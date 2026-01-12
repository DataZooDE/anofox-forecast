#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

namespace duckdb {

static LogicalType GetChangepointResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("changepoints", LogicalType::LIST(LogicalType::UBIGINT)));
    children.push_back(make_pair("n_changepoints", LogicalType::UBIGINT));
    children.push_back(make_pair("cost", LogicalType::DOUBLE));
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

static void TsDetectChangepointsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(list_vec, row_idx, values);

        ChangepointResult cp_result;
        memset(&cp_result, 0, sizeof(cp_result));
        AnofoxError error;

        bool success = anofox_ts_detect_changepoints(
            values.data(),
            values.size(),
            2,    // min_size
            0.0,  // penalty = auto
            &cp_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);

        // Set changepoints list
        {
            auto &cp_list = *children[0];
            auto list_data = FlatVector::GetData<list_entry_t>(cp_list);
            auto &list_child = ListVector::GetEntry(cp_list);
            auto current_size = ListVector::GetListSize(cp_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = cp_result.n_changepoints;

            ListVector::Reserve(cp_list, current_size + cp_result.n_changepoints);
            ListVector::SetListSize(cp_list, current_size + cp_result.n_changepoints);

            auto child_data = FlatVector::GetData<uint64_t>(list_child);
            if (cp_result.changepoints) {
                for (size_t i = 0; i < cp_result.n_changepoints; i++) {
                    child_data[current_size + i] = cp_result.changepoints[i];
                }
            }
        }

        // Set scalar fields
        FlatVector::GetData<uint64_t>(*children[1])[row_idx] = cp_result.n_changepoints;
        FlatVector::GetData<double>(*children[2])[row_idx] = cp_result.cost;

        anofox_free_changepoint_result(&cp_result);
    }
}

static void TsDetectChangepointsWithParamsFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    auto &min_size_vec = args.data[1];
    auto &penalty_vec = args.data[2];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Use UnifiedVectorFormat to handle both constant and flat vectors
    UnifiedVectorFormat min_size_data;
    min_size_vec.ToUnifiedFormat(count, min_size_data);

    UnifiedVectorFormat penalty_data;
    penalty_vec.ToUnifiedFormat(count, penalty_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(list_vec, row_idx, values);

        // Get min_size from unified format (handles constant vectors correctly)
        auto min_size_idx = min_size_data.sel->get_index(row_idx);
        int32_t min_size = 2;
        if (min_size_data.validity.RowIsValid(min_size_idx)) {
            min_size = UnifiedVectorFormat::GetData<int32_t>(min_size_data)[min_size_idx];
        }

        // Get penalty from unified format (handles constant vectors correctly)
        auto penalty_idx = penalty_data.sel->get_index(row_idx);
        double penalty = 0.0;
        if (penalty_data.validity.RowIsValid(penalty_idx)) {
            penalty = UnifiedVectorFormat::GetData<double>(penalty_data)[penalty_idx];
        }

        ChangepointResult cp_result;
        memset(&cp_result, 0, sizeof(cp_result));
        AnofoxError error;

        bool success = anofox_ts_detect_changepoints(
            values.data(),
            values.size(),
            min_size,
            penalty,
            &cp_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);

        // Set changepoints list
        {
            auto &cp_list = *children[0];
            auto list_data = FlatVector::GetData<list_entry_t>(cp_list);
            auto &list_child = ListVector::GetEntry(cp_list);
            auto current_size = ListVector::GetListSize(cp_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = cp_result.n_changepoints;

            ListVector::Reserve(cp_list, current_size + cp_result.n_changepoints);
            ListVector::SetListSize(cp_list, current_size + cp_result.n_changepoints);

            auto child_data = FlatVector::GetData<uint64_t>(list_child);
            if (cp_result.changepoints) {
                for (size_t i = 0; i < cp_result.n_changepoints; i++) {
                    child_data[current_size + i] = cp_result.changepoints[i];
                }
            }
        }

        FlatVector::GetData<uint64_t>(*children[1])[row_idx] = cp_result.n_changepoints;
        FlatVector::GetData<double>(*children[2])[row_idx] = cp_result.cost;

        anofox_free_changepoint_result(&cp_result);
    }
}

void RegisterTsDetectChangepointsFunction(ExtensionLoader &loader) {
    // No-op: C++ extension uses table macro, not scalar
    // Table macro ts_detect_changepoints is registered in ts_macros.cpp
}

// ============================================================================
// BOCPD version - C++ API compatible
// Returns: STRUCT(is_changepoint BOOLEAN[], changepoint_probability DOUBLE[])
// ============================================================================

static LogicalType GetBocpdResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("is_changepoint", LogicalType::LIST(LogicalType::BOOLEAN)));
    children.push_back(make_pair("changepoint_probability", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("changepoint_indices", LogicalType::LIST(LogicalType::UBIGINT)));
    return LogicalType::STRUCT(std::move(children));
}

static void TsDetectChangepointsBocpdFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    auto &lambda_vec = args.data[1];
    auto &probs_vec = args.data[2];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    UnifiedVectorFormat lambda_data;
    lambda_vec.ToUnifiedFormat(count, lambda_data);

    UnifiedVectorFormat probs_data;
    probs_vec.ToUnifiedFormat(count, probs_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(list_vec, row_idx, values);

        auto lambda_idx = lambda_data.sel->get_index(row_idx);
        double hazard_lambda = 250.0;
        if (lambda_data.validity.RowIsValid(lambda_idx)) {
            hazard_lambda = UnifiedVectorFormat::GetData<double>(lambda_data)[lambda_idx];
        }

        auto probs_idx = probs_data.sel->get_index(row_idx);
        bool include_probs = false;
        if (probs_data.validity.RowIsValid(probs_idx)) {
            include_probs = UnifiedVectorFormat::GetData<bool>(probs_data)[probs_idx];
        }

        BocpdResult bocpd_result;
        memset(&bocpd_result, 0, sizeof(bocpd_result));
        AnofoxError error;

        bool success = anofox_ts_detect_changepoints_bocpd(
            values.data(),
            values.size(),
            hazard_lambda,
            include_probs,
            &bocpd_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        auto &children = StructVector::GetEntries(result);

        // Set is_changepoint list
        {
            auto &cp_list = *children[0];
            auto list_data = FlatVector::GetData<list_entry_t>(cp_list);
            auto &list_child = ListVector::GetEntry(cp_list);
            auto current_size = ListVector::GetListSize(cp_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = bocpd_result.n_points;

            ListVector::Reserve(cp_list, current_size + bocpd_result.n_points);
            ListVector::SetListSize(cp_list, current_size + bocpd_result.n_points);

            auto child_data = FlatVector::GetData<bool>(list_child);
            if (bocpd_result.is_changepoint) {
                for (size_t i = 0; i < bocpd_result.n_points; i++) {
                    child_data[current_size + i] = bocpd_result.is_changepoint[i];
                }
            }
        }

        // Set changepoint_probability list
        {
            auto &prob_list = *children[1];
            auto list_data = FlatVector::GetData<list_entry_t>(prob_list);
            auto &list_child = ListVector::GetEntry(prob_list);
            auto current_size = ListVector::GetListSize(prob_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = bocpd_result.n_points;

            ListVector::Reserve(prob_list, current_size + bocpd_result.n_points);
            ListVector::SetListSize(prob_list, current_size + bocpd_result.n_points);

            auto child_data = FlatVector::GetData<double>(list_child);
            if (bocpd_result.changepoint_probability) {
                for (size_t i = 0; i < bocpd_result.n_points; i++) {
                    child_data[current_size + i] = bocpd_result.changepoint_probability[i];
                }
            }
        }

        // Set changepoint_indices list
        {
            auto &idx_list = *children[2];
            auto list_data = FlatVector::GetData<list_entry_t>(idx_list);
            auto &list_child = ListVector::GetEntry(idx_list);
            auto current_size = ListVector::GetListSize(idx_list);

            list_data[row_idx].offset = current_size;
            list_data[row_idx].length = bocpd_result.n_changepoints;

            ListVector::Reserve(idx_list, current_size + bocpd_result.n_changepoints);
            ListVector::SetListSize(idx_list, current_size + bocpd_result.n_changepoints);

            auto child_data = FlatVector::GetData<uint64_t>(list_child);
            if (bocpd_result.changepoint_indices) {
                for (size_t i = 0; i < bocpd_result.n_changepoints; i++) {
                    child_data[current_size + i] = bocpd_result.changepoint_indices[i];
                }
            }
        }

        anofox_free_bocpd_result(&bocpd_result);
    }
}

void RegisterTsDetectChangepointsBocpdFunction(ExtensionLoader &loader) {
    // Internal scalar function used by ts_detect_changepoints table macros
    ScalarFunctionSet ts_bocpd_set("_ts_detect_changepoints_bocpd");

    // _ts_detect_changepoints_bocpd(values, hazard_lambda, include_probabilities)
    ts_bocpd_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE, LogicalType::BOOLEAN},
        GetBocpdResultType(),
        TsDetectChangepointsBocpdFunction
    ));

    // Mark as internal to hide from duckdb_functions() and deprioritize in autocomplete
    CreateScalarFunctionInfo info(ts_bocpd_set);
    info.internal = true;
    loader.RegisterFunction(info);
}

// Placeholder functions
void RegisterTsDetectChangepointsByFunction(ExtensionLoader &loader) {
    // TODO: Table function for partitioned changepoint detection
}

// Note: RegisterTsDetectChangepointsAggFunction is implemented in ts_changepoints_agg.cpp

} // namespace duckdb
