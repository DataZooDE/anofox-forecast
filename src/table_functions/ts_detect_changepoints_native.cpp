#include "ts_detect_changepoints_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For DateToMicroseconds, etc.
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
static string GetChangepointGroupKey(const Value &group_value) {
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
    string date_col_name;
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
    LogicalType date_logical_type = LogicalType(LogicalTypeId::DATE);
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
};

// ============================================================================
// Local State - buffers data per thread, processes incrementally
// ============================================================================

struct TsDetectChangepointsNativeLocalState : public LocalTableFunctionState {
    // Buffer for incoming data per group - use primitives for efficiency
    struct GroupData {
        Value group_value;
        vector<int64_t> dates;    // Store as int64_t (microseconds or raw value)
        vector<double> values;
    };

    // Map group key -> accumulated data
    std::map<string, GroupData> groups;
    vector<string> group_order;  // Track insertion order

    // Current group being output
    idx_t current_group_idx = 0;

    // Current results for the group being output (processed on demand)
    struct CurrentGroupOutput {
        bool valid = false;
        Value group_value;
        vector<int64_t> dates;
        vector<double> values;
        vector<bool> is_changepoint;
        vector<double> changepoint_probability;
        idx_t current_row = 0;
    };
    CurrentGroupOutput current_output;
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

    // Parse parameters
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

    // Store column info
    bind_data->group_col_name = input.input_table_names[0];
    bind_data->group_logical_type = input.input_table_types[0];
    bind_data->date_col_name = input.input_table_names[1];
    bind_data->date_logical_type = input.input_table_types[1];

    // Determine date column type
    auto date_type_id = input.input_table_types[1].id();
    if (date_type_id == LogicalTypeId::DATE) {
        bind_data->date_col_type = DateColumnType::DATE;
    } else if (date_type_id == LogicalTypeId::TIMESTAMP ||
               date_type_id == LogicalTypeId::TIMESTAMP_TZ ||
               date_type_id == LogicalTypeId::TIMESTAMP_NS ||
               date_type_id == LogicalTypeId::TIMESTAMP_MS ||
               date_type_id == LogicalTypeId::TIMESTAMP_SEC) {
        bind_data->date_col_type = DateColumnType::TIMESTAMP;
    } else {
        bind_data->date_col_type = DateColumnType::INTEGER;
    }

    // Output schema: row-per-point format
    names.push_back(bind_data->group_col_name);
    return_types.push_back(bind_data->group_logical_type);

    names.push_back(bind_data->date_col_name);
    return_types.push_back(bind_data->date_logical_type);

    names.push_back("value");
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back("is_changepoint");
    return_types.push_back(LogicalType::BOOLEAN);

    names.push_back("changepoint_probability");
    return_types.push_back(LogicalType::DOUBLE);

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

    auto &bind_data = data_p.bind_data->Cast<TsDetectChangepointsNativeBindData>();
    auto &local_state = data_p.local_state->Cast<TsDetectChangepointsNativeLocalState>();

    // Buffer all incoming data - we need complete groups before processing
    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull()) continue;

        string group_key = GetChangepointGroupKey(group_val);

        if (local_state.groups.find(group_key) == local_state.groups.end()) {
            local_state.groups[group_key] = TsDetectChangepointsNativeLocalState::GroupData();
            local_state.groups[group_key].group_value = group_val;
            local_state.group_order.push_back(group_key);
        }

        auto &grp = local_state.groups[group_key];

        // Convert date to int64_t based on type
        int64_t date_int;
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                date_int = DateToMicroseconds(date_val.GetValue<date_t>());
                break;
            case DateColumnType::TIMESTAMP:
                date_int = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
                break;
            default:
                date_int = date_val.GetValue<int64_t>();
                break;
        }

        grp.dates.push_back(date_int);
        grp.values.push_back(value_val.IsNull() ? 0.0 : value_val.GetValue<double>());
    }

    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Helper: Process a single group and populate current_output
// ============================================================================

static void ProcessChangepointGroup(
    TsDetectChangepointsNativeLocalState &local_state,
    const TsDetectChangepointsNativeBindData &bind_data,
    const string &group_key) {

    auto &grp = local_state.groups[group_key];
    auto &out = local_state.current_output;

    out.valid = true;
    out.group_value = grp.group_value;
    out.dates = std::move(grp.dates);
    out.values = std::move(grp.values);
    out.current_row = 0;

    size_t n_points = out.values.size();

    // Need at least 3 points for BOCPD changepoint detection
    if (n_points < 3) {
        out.is_changepoint.resize(n_points, false);
        out.changepoint_probability.resize(n_points, 0.0);
        return;
    }

    // Call Rust FFI for BOCPD changepoint detection
    BocpdResult bocpd_result = {};
    AnofoxError error = {};

    bool success = anofox_ts_detect_changepoints_bocpd(
        out.values.data(),
        out.values.size(),
        bind_data.hazard_lambda,
        bind_data.include_probabilities,
        &bocpd_result,
        &error
    );

    if (!success) {
        string error_msg = error.message ? error.message : "Unknown error";
        throw InvalidInputException("_ts_detect_changepoints_native failed: %s", error_msg.c_str());
    }

    // Copy results
    out.is_changepoint.resize(n_points);
    out.changepoint_probability.resize(n_points);

    for (size_t i = 0; i < n_points; i++) {
        out.is_changepoint[i] = (bocpd_result.is_changepoint && i < bocpd_result.n_points)
            ? bocpd_result.is_changepoint[i] : false;
        out.changepoint_probability[i] = (bocpd_result.changepoint_probability && i < bocpd_result.n_points)
            ? bocpd_result.changepoint_probability[i] : 0.0;
    }

    anofox_free_bocpd_result(&bocpd_result);
}

// ============================================================================
// Finalize Function - process groups incrementally and output results
// ============================================================================

static OperatorFinalizeResultType TsDetectChangepointsNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsDetectChangepointsNativeBindData>();
    auto &local_state = data_p.local_state->Cast<TsDetectChangepointsNativeLocalState>();

    idx_t output_count = 0;

    while (output_count < STANDARD_VECTOR_SIZE) {
        // If no current group being output, process next group
        if (!local_state.current_output.valid) {
            if (local_state.current_group_idx >= local_state.group_order.size()) {
                break;
            }

            const string &group_key = local_state.group_order[local_state.current_group_idx];
            ProcessChangepointGroup(local_state, bind_data, group_key);
            local_state.groups.erase(group_key);
        }

        auto &out = local_state.current_output;

        // Output rows from current group
        while (output_count < STANDARD_VECTOR_SIZE && out.current_row < out.values.size()) {
            idx_t out_idx = output_count;
            idx_t row = out.current_row;

            // Column 0: Group column
            output.data[0].SetValue(out_idx, out.group_value);

            // Column 1: Date column - convert back from int64_t
            Value date_value;
            switch (bind_data.date_col_type) {
                case DateColumnType::DATE:
                    date_value = Value::DATE(MicrosecondsToDate(out.dates[row]));
                    break;
                case DateColumnType::TIMESTAMP:
                    date_value = Value::TIMESTAMP(MicrosecondsToTimestamp(out.dates[row]));
                    break;
                default:
                    date_value = Value::BIGINT(out.dates[row]);
                    break;
            }
            output.data[1].SetValue(out_idx, date_value);

            // Column 2: value
            output.data[2].SetValue(out_idx, Value::DOUBLE(out.values[row]));

            // Column 3: is_changepoint
            output.data[3].SetValue(out_idx, Value::BOOLEAN(out.is_changepoint[row]));

            // Column 4: changepoint_probability
            output.data[4].SetValue(out_idx, Value::DOUBLE(out.changepoint_probability[row]));

            output_count++;
            out.current_row++;
        }

        // If we've output all rows for this group, move to next
        if (out.current_row >= out.values.size()) {
            out.valid = false;
            out.dates.clear();
            out.dates.shrink_to_fit();
            out.values.clear();
            out.values.shrink_to_fit();
            out.is_changepoint.clear();
            out.is_changepoint.shrink_to_fit();
            out.changepoint_probability.clear();
            out.changepoint_probability.shrink_to_fit();

            local_state.current_group_idx++;
        }
    }

    output.SetCardinality(output_count);

    if (output_count == 0) {
        return OperatorFinalizeResultType::FINISHED;
    }

    if (local_state.current_output.valid ||
        local_state.current_group_idx < local_state.group_order.size()) {
        return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
    }

    return OperatorFinalizeResultType::FINISHED;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsDetectChangepointsNativeFunction(ExtensionLoader &loader) {
    TableFunction func("_ts_detect_changepoints_native",
        {LogicalType::TABLE, LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::BOOLEAN)},
        nullptr,
        TsDetectChangepointsNativeBind,
        TsDetectChangepointsNativeInitGlobal,
        TsDetectChangepointsNativeInitLocal);

    func.in_out_function = TsDetectChangepointsNativeInOut;
    func.in_out_function_final = TsDetectChangepointsNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
