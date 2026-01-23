#include "ts_detect_changepoints_native.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>

namespace duckdb {

// ============================================================================
// Helper Functions
// ============================================================================

// Generate group key for map lookup
static string GetGroupKey(const Value &group_value) {
    if (group_value.IsNull()) {
        return "__NULL__";
    }
    return group_value.ToString();
}

// ============================================================================
// Bind Data
// ============================================================================

struct TsDetectChangepointsNativeBindData : public TableFunctionData {
    double hazard_lambda = 250.0;
    bool include_probabilities = false;
    string group_col_name;
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
};

// ============================================================================
// Local State - buffers data per thread
// ============================================================================

struct TsDetectChangepointsNativeLocalState : public LocalTableFunctionState {
    // Buffer for incoming data per group
    struct GroupData {
        Value group_value;
        vector<double> values;
        vector<bool> validity;
    };

    // Map group key -> accumulated data
    std::map<string, GroupData> groups;
    vector<string> group_order;  // Track insertion order

    // Changepoint results ready to output
    struct ChangepointGroup {
        Value group_value;
        vector<bool> is_changepoint;
        vector<double> changepoint_probability;
        vector<uint64_t> changepoint_indices;
        uint64_t n_changepoints;
    };
    vector<ChangepointGroup> changepoint_results;
    idx_t current_group = 0;
    bool processed = false;
};

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsDetectChangepointsNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsDetectChangepointsNativeBindData>();

    // Parse parameters from second argument (MAP as JSON string)
    // The macro converts params MAP to individual values
    if (input.inputs.size() >= 2 && !input.inputs[1].IsNull()) {
        bind_data->hazard_lambda = input.inputs[1].GetValue<double>();
    }
    if (input.inputs.size() >= 3 && !input.inputs[2].IsNull()) {
        bind_data->include_probabilities = input.inputs[2].GetValue<bool>();
    }

    // Input table must have exactly 3 columns: group, date, value
    if (input.input_table_types.size() != 3) {
        throw InvalidInputException(
            "_ts_detect_changepoints_native requires input with exactly 3 columns: group_col, date_col, value_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Store group column info
    bind_data->group_col_name = input.input_table_names[0];
    bind_data->group_logical_type = input.input_table_types[0];

    // Output schema: preserve original group column name + changepoint columns
    names.push_back(bind_data->group_col_name);
    return_types.push_back(bind_data->group_logical_type);

    // Changepoint output columns
    names.push_back("is_changepoint");
    return_types.push_back(LogicalType::LIST(LogicalType(LogicalTypeId::BOOLEAN)));

    names.push_back("changepoint_probability");
    return_types.push_back(LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)));

    names.push_back("changepoint_indices");
    return_types.push_back(LogicalType::LIST(LogicalType(LogicalTypeId::UBIGINT)));

    names.push_back("n_changepoints");
    return_types.push_back(LogicalType(LogicalTypeId::UBIGINT));

    return bind_data;
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsDetectChangepointsNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<GlobalTableFunctionState>();
}

static unique_ptr<LocalTableFunctionState> TsDetectChangepointsNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsDetectChangepointsNativeLocalState>();
}

// ============================================================================
// In-Out Function - receives streaming input
// ============================================================================

static OperatorResultType TsDetectChangepointsNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &local_state = data_p.local_state->Cast<TsDetectChangepointsNativeLocalState>();

    // Buffer all incoming data - we need complete groups before processing
    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull()) continue;

        string group_key = GetGroupKey(group_val);

        if (local_state.groups.find(group_key) == local_state.groups.end()) {
            local_state.groups[group_key] = TsDetectChangepointsNativeLocalState::GroupData();
            local_state.groups[group_key].group_value = group_val;
            local_state.group_order.push_back(group_key);
        }

        auto &grp = local_state.groups[group_key];

        // Store value (we need to sort by date later, but for changepoints order matters)
        // For now, assume data comes in date order from the macro's ORDER BY
        grp.values.push_back(value_val.IsNull() ? 0.0 : value_val.GetValue<double>());
        grp.validity.push_back(!value_val.IsNull());
    }

    // Don't output anything during input phase - wait for finalize
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - process accumulated data and output results
// ============================================================================

static OperatorFinalizeResultType TsDetectChangepointsNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsDetectChangepointsNativeBindData>();
    auto &local_state = data_p.local_state->Cast<TsDetectChangepointsNativeLocalState>();

    // Process all groups on first finalize call
    if (!local_state.processed) {
        for (const auto &group_key : local_state.group_order) {
            auto &grp = local_state.groups[group_key];

            TsDetectChangepointsNativeLocalState::ChangepointGroup cp_grp;
            cp_grp.group_value = grp.group_value;

            // Need at least 3 points for BOCPD changepoint detection
            if (grp.values.size() < 3) {
                // Return empty results for too-small series
                cp_grp.n_changepoints = 0;
                local_state.changepoint_results.push_back(std::move(cp_grp));
                continue;
            }

            // Call Rust FFI for BOCPD changepoint detection
            BocpdResult bocpd_result = {};
            AnofoxError error = {};

            bool success = anofox_ts_detect_changepoints_bocpd(
                grp.values.data(),
                grp.values.size(),
                bind_data.hazard_lambda,
                bind_data.include_probabilities,
                &bocpd_result,
                &error
            );

            if (!success) {
                throw InvalidInputException("_ts_detect_changepoints_native failed: %s",
                    error.message ? error.message : "Unknown error");
            }

            // Copy results
            cp_grp.n_changepoints = bocpd_result.n_changepoints;

            // Copy is_changepoint array
            if (bocpd_result.is_changepoint && bocpd_result.n_points > 0) {
                cp_grp.is_changepoint.resize(bocpd_result.n_points);
                for (size_t i = 0; i < bocpd_result.n_points; i++) {
                    cp_grp.is_changepoint[i] = bocpd_result.is_changepoint[i];
                }
            }

            // Copy changepoint_probability array
            if (bocpd_result.changepoint_probability && bocpd_result.n_points > 0) {
                cp_grp.changepoint_probability.resize(bocpd_result.n_points);
                for (size_t i = 0; i < bocpd_result.n_points; i++) {
                    cp_grp.changepoint_probability[i] = bocpd_result.changepoint_probability[i];
                }
            }

            // Copy changepoint_indices array
            if (bocpd_result.changepoint_indices && bocpd_result.n_changepoints > 0) {
                cp_grp.changepoint_indices.resize(bocpd_result.n_changepoints);
                for (size_t i = 0; i < bocpd_result.n_changepoints; i++) {
                    cp_grp.changepoint_indices[i] = bocpd_result.changepoint_indices[i];
                }
            }

            local_state.changepoint_results.push_back(std::move(cp_grp));

            anofox_free_bocpd_result(&bocpd_result);
        }
        local_state.processed = true;
    }

    // Output results
    if (local_state.changepoint_results.empty() ||
        local_state.current_group >= local_state.changepoint_results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t output_count = 0;

    while (output_count < STANDARD_VECTOR_SIZE &&
           local_state.current_group < local_state.changepoint_results.size()) {

        auto &grp = local_state.changepoint_results[local_state.current_group];
        idx_t out_idx = output_count;

        // Column 0: Group column (preserve original type and name)
        output.data[0].SetValue(out_idx, grp.group_value);

        // Column 1: is_changepoint (LIST of BOOLEAN)
        {
            vector<Value> values;
            values.reserve(grp.is_changepoint.size());
            for (auto v : grp.is_changepoint) {
                values.push_back(Value::BOOLEAN(v));
            }
            output.data[1].SetValue(out_idx, Value::LIST(LogicalType::BOOLEAN, std::move(values)));
        }

        // Column 2: changepoint_probability (LIST of DOUBLE)
        {
            vector<Value> values;
            values.reserve(grp.changepoint_probability.size());
            for (auto v : grp.changepoint_probability) {
                values.push_back(Value::DOUBLE(v));
            }
            output.data[2].SetValue(out_idx, Value::LIST(LogicalType::DOUBLE, std::move(values)));
        }

        // Column 3: changepoint_indices (LIST of UBIGINT)
        {
            vector<Value> values;
            values.reserve(grp.changepoint_indices.size());
            for (auto v : grp.changepoint_indices) {
                values.push_back(Value::UBIGINT(v));
            }
            output.data[3].SetValue(out_idx, Value::LIST(LogicalType::UBIGINT, std::move(values)));
        }

        // Column 4: n_changepoints (UBIGINT)
        output.data[4].SetValue(out_idx, Value::UBIGINT(grp.n_changepoints));

        output_count++;
        local_state.current_group++;
    }

    output.SetCardinality(output_count);

    if (local_state.current_group >= local_state.changepoint_results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsDetectChangepointsNativeFunction(ExtensionLoader &loader) {
    // Internal table-in-out function: (TABLE, hazard_lambda, include_probabilities)
    // Input table must have 3 columns: group_col, date_col, value_col
    // Note: This is an internal function (prefixed with _) called by ts_detect_changepoints_by macro
    TableFunction func("_ts_detect_changepoints_native",
        {LogicalType::TABLE, LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::BOOLEAN)},
        nullptr,  // No execute function - use in_out_function
        TsDetectChangepointsNativeBind,
        TsDetectChangepointsNativeInitGlobal,
        TsDetectChangepointsNativeInitLocal);

    func.in_out_function = TsDetectChangepointsNativeInOut;
    func.in_out_function_final = TsDetectChangepointsNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
