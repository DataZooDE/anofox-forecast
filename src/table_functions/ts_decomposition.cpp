#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "ts_list_marshal.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/common/types/vector.hpp"
#include <algorithm>
#include <cctype>

namespace duckdb {

static LogicalType GetMstlResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("trend", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("seasonal", LogicalType::LIST(LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)))));
    children.push_back(make_pair("remainder", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("periods", LogicalType::LIST(LogicalType(LogicalTypeId::INTEGER))));
    return LogicalType::STRUCT(std::move(children));
}

// Helper to convert insufficient_data mode string to int
static int ParseInsufficientDataMode(const string &mode) {
    string lower = mode;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "trend") return 1;
    if (lower == "none") return 2;
    return 0;  // default: fail
}

// Reads a LIST(INTEGER) argument for every row of a chunk (Phase 15 plan
// 15-04, DET-04). Mirrors ListDoubleReader's unified-format discipline but
// for an INTEGER periods list rather than series data: a NULL top-level
// list reads as "no periods requested"; a NULL element is simply skipped
// (the ts_mstl_decomposition_by macro's CAST to INTEGER[] already turns an
// unparsable entry into an SQL error before this function ever runs).
class ListIntReader {
public:
    ListIntReader(Vector &list_vec, idx_t count) : list_vec(list_vec) {
        list_vec.ToUnifiedFormat(count, list_data);
        auto &child_vec_ref = ListVector::GetEntry(list_vec);
        child_vec_ref.ToUnifiedFormat(ListVector::GetListSize(list_vec), child_data);
    }

    bool IsNull(idx_t row_idx) const {
        auto list_idx = list_data.sel->get_index(row_idx);
        return !list_data.validity.RowIsValid(list_idx);
    }

    void Read(idx_t row_idx, vector<int32_t> &values) const {
        auto list_entries = UnifiedVectorFormat::GetData<list_entry_t>(list_data);
        auto list_idx = list_data.sel->get_index(row_idx);
        auto &list_entry = list_entries[list_idx];
        auto child_values = UnifiedVectorFormat::GetData<int32_t>(child_data);

        idx_t n = list_entry.length;
        idx_t list_offset = list_entry.offset;
        values.clear();
        values.reserve(n);
        for (idx_t i = 0; i < n; i++) {
            idx_t child_idx = list_offset + i;
            auto unified_child_idx = child_data.sel->get_index(child_idx);
            if (child_data.validity.RowIsValid(unified_child_idx)) {
                values.push_back(child_values[unified_child_idx]);
            }
        }
    }

private:
    Vector &list_vec;
    UnifiedVectorFormat list_data;
    UnifiedVectorFormat child_data;
};

static void TsMstlDecompositionFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    ListDoubleReader values_reader(list_vec, count);
    bool has_mode = args.ColumnCount() > 1;
    unique_ptr<ScalarArgReader<string_t>> mode_reader;
    if (has_mode) {
        mode_reader = make_uniq<ScalarArgReader<string_t>>(args.data[1], count);
    }
    bool has_periods = args.ColumnCount() > 2;
    unique_ptr<ListIntReader> periods_reader;
    if (has_periods) {
        periods_reader = make_uniq<ListIntReader>(args.data[2], count);
    }

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        vector<double> values;
        if (!values_reader.ReadSingleSeries(row_idx, values, "_ts_mstl_decomposition")) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Get insufficient_data mode from second argument if provided (default: fail = 0)
        int insufficient_data_mode = 0;
        if (has_mode && !mode_reader->IsNull(row_idx)) {
            insufficient_data_mode = ParseInsufficientDataMode(mode_reader->Get(row_idx).GetString());
        }

        // Get seasonal periods from third argument if provided (default: none, D-09 auto)
        vector<int32_t> periods_vec;
        if (has_periods && !periods_reader->IsNull(row_idx)) {
            periods_reader->Read(row_idx, periods_vec);
        }

        MstlResult mstl_result;
        memset(&mstl_result, 0, sizeof(mstl_result));
        AnofoxError error;

        bool success = anofox_ts_mstl_decomposition(
            values.data(),
            values.size(),
            periods_vec.empty() ? nullptr : periods_vec.data(),
            periods_vec.size(),
            insufficient_data_mode,
            &mstl_result,
            &error
        );

        if (!success) {
            if (error.code == INVALID_INPUT) {
                throw InvalidInputException("_ts_mstl_decomposition: %s",
                    error.message ? error.message : "invalid input");
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);

        // Set trend list
        {
            auto &trend_list = *children[0];
            auto list_data = FlatVector::GetData<list_entry_t>(trend_list);
            auto &list_child = ListVector::GetEntry(trend_list);
            auto current_size = ListVector::GetListSize(trend_list);

            // CR-01: trend is a null pointer when decomposition was not applied
            // (insufficient_data_mode = 'none' with no usable period); n_observations
            // still reflects the full input length in that case. Emit an empty list
            // rather than exposing uninitialized/stale ListVector buffer memory.
            idx_t trend_len = mstl_result.trend ? mstl_result.n_observations : 0;

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = trend_len;

            ListVector::Reserve(trend_list, current_size + trend_len);
            ListVector::SetListSize(trend_list, current_size + trend_len);

            auto child_data = FlatVector::GetData<double>(list_child);
            if (mstl_result.trend) {
                memcpy(child_data + current_size, mstl_result.trend,
                       trend_len * sizeof(double));
            }
        }

        // Set seasonal components (list of lists)
        {
            auto &seasonal_outer = *children[1];
            auto outer_list_data = FlatVector::GetData<list_entry_t>(seasonal_outer);
            auto &inner_list_vec = ListVector::GetEntry(seasonal_outer);
            auto outer_size = ListVector::GetListSize(seasonal_outer);

            outer_list_data[row_idx].offset = outer_size;
            outer_list_data[row_idx].length = mstl_result.n_seasonal;

            ListVector::Reserve(seasonal_outer, outer_size + mstl_result.n_seasonal);
            ListVector::SetListSize(seasonal_outer, outer_size + mstl_result.n_seasonal);

            auto inner_list_data = FlatVector::GetData<list_entry_t>(inner_list_vec);
            auto &inner_child = ListVector::GetEntry(inner_list_vec);

            for (size_t s = 0; s < mstl_result.n_seasonal; s++) {
                auto inner_size = ListVector::GetListSize(inner_list_vec);
                inner_list_data[outer_size + s].offset = inner_size;
                inner_list_data[outer_size + s].length = mstl_result.n_observations;

                ListVector::Reserve(inner_list_vec, inner_size + mstl_result.n_observations);
                ListVector::SetListSize(inner_list_vec, inner_size + mstl_result.n_observations);

                auto inner_data = FlatVector::GetData<double>(inner_child);
                if (mstl_result.seasonal_components && mstl_result.seasonal_components[s]) {
                    memcpy(inner_data + inner_size, mstl_result.seasonal_components[s],
                           mstl_result.n_observations * sizeof(double));
                }
            }
        }

        // Set remainder list
        {
            auto &remainder_list = *children[2];
            auto list_data = FlatVector::GetData<list_entry_t>(remainder_list);
            auto &list_child = ListVector::GetEntry(remainder_list);
            auto current_size = ListVector::GetListSize(remainder_list);

            // CR-01: mirror the trend guard above -- remainder is null when
            // decomposition was not applied, so emit an empty list instead of
            // reading uninitialized ListVector buffer memory.
            idx_t remainder_len = mstl_result.remainder ? mstl_result.n_observations : 0;

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = remainder_len;

            ListVector::Reserve(remainder_list, current_size + remainder_len);
            ListVector::SetListSize(remainder_list, current_size + remainder_len);

            auto child_data = FlatVector::GetData<double>(list_child);
            if (mstl_result.remainder) {
                memcpy(child_data + current_size, mstl_result.remainder,
                       remainder_len * sizeof(double));
            }
        }

        // Set periods list
        {
            auto &periods_list = *children[3];
            auto list_data = FlatVector::GetData<list_entry_t>(periods_list);
            auto &list_child = ListVector::GetEntry(periods_list);
            auto current_size = ListVector::GetListSize(periods_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = mstl_result.n_seasonal;

            ListVector::Reserve(periods_list, current_size + mstl_result.n_seasonal);
            ListVector::SetListSize(periods_list, current_size + mstl_result.n_seasonal);

            auto child_data = FlatVector::GetData<int32_t>(list_child);
            if (mstl_result.seasonal_periods) {
                for (size_t i = 0; i < mstl_result.n_seasonal; i++) {
                    child_data[current_size + i] = mstl_result.seasonal_periods[i];
                }
            }
        }

        anofox_free_mstl_result(&mstl_result);
    }
}

void RegisterTsMstlDecompositionFunction(ExtensionLoader &loader) {
    // Internal scalar function used by ts_mstl_decomposition table macro
    ScalarFunctionSet ts_mstl_set("_ts_mstl_decomposition");
    // 1-arg version: just values
    ts_mstl_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        GetMstlResultType(),
        TsMstlDecompositionFunction
    ));
    // 2-arg version: values + insufficient_data mode
    ts_mstl_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::VARCHAR)},
        GetMstlResultType(),
        TsMstlDecompositionFunction
    ));
    // 3-arg version: values + insufficient_data mode + seasonal periods (D-09)
    ts_mstl_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::VARCHAR),
         LogicalType::LIST(LogicalType(LogicalTypeId::INTEGER))},
        GetMstlResultType(),
        TsMstlDecompositionFunction
    ));

    // Mark as internal to hide from duckdb_functions() and deprioritize in autocomplete
    CreateScalarFunctionInfo info(ts_mstl_set);
    info.internal = true;
    loader.RegisterFunction(info);
}

} // namespace duckdb
