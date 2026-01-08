#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include "duckdb/function/scalar_function.hpp"
#include <algorithm>
#include <cctype>

namespace duckdb {

static LogicalType GetMstlResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("trend", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("seasonal", LogicalType::LIST(LogicalType::LIST(LogicalType::DOUBLE))));
    children.push_back(make_pair("remainder", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("periods", LogicalType::LIST(LogicalType::INTEGER)));
    return LogicalType::STRUCT(std::move(children));
}

static void ExtractListAsDouble(Vector &list_vec, idx_t row_idx, vector<double> &out_values) {
    auto list_data = ListVector::GetData(list_vec);
    auto &list_entry = list_data[row_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);
    auto child_data = FlatVector::GetData<double>(child_vec);
    auto &child_validity = FlatVector::Validity(child_vec);

    out_values.clear();
    out_values.reserve(list_entry.length);

    for (idx_t i = 0; i < list_entry.length; i++) {
        idx_t child_idx = list_entry.offset + i;
        if (child_validity.RowIsValid(child_idx)) {
            out_values.push_back(child_data[child_idx]);
        }
    }
}

// Helper to convert insufficient_data mode string to int
static int ParseInsufficientDataMode(const string &mode) {
    string lower = mode;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "trend") return 1;
    if (lower == "none") return 2;
    return 0;  // default: fail
}

static void TsMstlDecompositionFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    // Get insufficient_data mode from second argument if provided (default: fail = 0)
    int insufficient_data_mode = 0;
    if (args.ColumnCount() > 1) {
        auto &mode_vec = args.data[1];
        if (!FlatVector::IsNull(mode_vec, 0)) {
            auto mode_str = FlatVector::GetData<string_t>(mode_vec)[0].GetString();
            insufficient_data_mode = ParseInsufficientDataMode(mode_str);
        }
    }

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(list_vec, row_idx, values);

        MstlResult mstl_result;
        memset(&mstl_result, 0, sizeof(mstl_result));
        AnofoxError error;

        bool success = anofox_ts_mstl_decomposition(
            values.data(),
            values.size(),
            nullptr,  // periods - auto detect
            0,
            insufficient_data_mode,
            &mstl_result,
            &error
        );

        if (!success) {
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

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = mstl_result.n_observations;

            ListVector::Reserve(trend_list, current_size + mstl_result.n_observations);
            ListVector::SetListSize(trend_list, current_size + mstl_result.n_observations);

            auto child_data = FlatVector::GetData<double>(list_child);
            if (mstl_result.trend) {
                memcpy(child_data + current_size, mstl_result.trend,
                       mstl_result.n_observations * sizeof(double));
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

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = mstl_result.n_observations;

            ListVector::Reserve(remainder_list, current_size + mstl_result.n_observations);
            ListVector::SetListSize(remainder_list, current_size + mstl_result.n_observations);

            auto child_data = FlatVector::GetData<double>(list_child);
            if (mstl_result.remainder) {
                memcpy(child_data + current_size, mstl_result.remainder,
                       mstl_result.n_observations * sizeof(double));
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
        {LogicalType::LIST(LogicalType::DOUBLE)},
        GetMstlResultType(),
        TsMstlDecompositionFunction
    ));
    // 2-arg version: values + insufficient_data mode
    ts_mstl_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::VARCHAR},
        GetMstlResultType(),
        TsMstlDecompositionFunction
    ));
    loader.RegisterFunction(ts_mstl_set);
}

} // namespace duckdb
