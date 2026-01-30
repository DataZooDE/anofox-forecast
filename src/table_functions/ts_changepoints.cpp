#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "ts_fill_gaps_native.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include <map>
#include <mutex>
#include <regex>
#include <set>

namespace duckdb {

static LogicalType GetChangepointResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("changepoints", LogicalType::LIST(LogicalType(LogicalTypeId::UBIGINT))));
    children.push_back(make_pair("n_changepoints", LogicalType(LogicalTypeId::UBIGINT)));
    children.push_back(make_pair("cost", LogicalType(LogicalTypeId::DOUBLE)));
    return LogicalType::STRUCT(std::move(children));
}

static void ExtractListAsDouble(Vector &list_vec, idx_t row_idx, vector<double> &out_values) {
    auto list_data = ListVector::GetData(list_vec);
    auto &list_entry = list_data[row_idx];

    out_values.clear();

    // Early return for empty lists to avoid accessing uninitialized child vector
    if (list_entry.length == 0) {
        return;
    }

    auto &child_vec = ListVector::GetEntry(list_vec);
    auto child_data = FlatVector::GetData<double>(child_vec);
    auto &child_validity = FlatVector::Validity(child_vec);

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

        // Handle empty or too small arrays - need at least 2 points for changepoint detection
        if (values.size() < 2) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

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

        // Handle empty or too small arrays - need at least 2 points for changepoint detection
        if (values.size() < 2) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

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
    children.push_back(make_pair("is_changepoint", LogicalType::LIST(LogicalType(LogicalTypeId::BOOLEAN))));
    children.push_back(make_pair("changepoint_probability", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("changepoint_indices", LogicalType::LIST(LogicalType(LogicalTypeId::UBIGINT))));
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

        // Handle empty or too small arrays - need at least 2 points for changepoint detection
        if (values.size() < 2) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

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
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::BOOLEAN)},
        GetBocpdResultType(),
        TsDetectChangepointsBocpdFunction
    ));

    // Mark as internal to hide from duckdb_functions() and deprioritize in autocomplete
    CreateScalarFunctionInfo info(ts_bocpd_set);
    info.internal = true;
    loader.RegisterFunction(info);
}

// ============================================================================
// ts_detect_changepoints_by - Native Table Function
// ============================================================================
// Returns row-level changepoint detection results with preserved column names.
// Output: group_col (preserved name), date_col, is_changepoint, changepoint_probability

struct TsDetectChangepointsByBindData : public TableFunctionData {
    double hazard_lambda = 250.0;
    bool include_probabilities = true;  // Always include for row-level output
    string group_col_name;
    string date_col_name;
    string value_col_name;
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
};

// ============================================================================
// Global State - enables parallel execution
//
// IMPORTANT: This custom GlobalState is required for proper parallel execution.
// Using the base GlobalTableFunctionState directly causes batch index collisions
// with large datasets (300k+ groups) during BatchedDataCollection::Merge.
// ============================================================================

struct TsDetectChangepointsByGlobalState : public GlobalTableFunctionState {
    // Allow parallel execution - each thread processes its partition of groups
    idx_t MaxThreads() const override {
        return 999999;  // Unlimited - let DuckDB decide based on hardware
    }

    // Global group tracking to prevent duplicate processing
    std::mutex processed_groups_mutex;
    std::set<string> processed_groups;

    bool ClaimGroup(const string &group_key) {
        std::lock_guard<std::mutex> lock(processed_groups_mutex);
        auto result = processed_groups.insert(group_key);
        return result.second;
    }
};

struct TsDetectChangepointsByLocalState : public LocalTableFunctionState {
    struct GroupData {
        Value group_value;
        vector<int64_t> timestamps;
        vector<double> values;
    };

    std::map<string, GroupData> groups;
    vector<string> group_order;

    struct OutputRow {
        Value group_value;
        int64_t timestamp;
        bool is_changepoint;
        double changepoint_probability;
    };
    vector<OutputRow> results;

    bool processed = false;
    idx_t output_offset = 0;
};

// Parse hazard_lambda from params string (simple numeric value or JSON-like format)
static double ParseHazardLambda(const string &params_str) {
    string trimmed = params_str;
    // Trim whitespace
    while (!trimmed.empty() && (trimmed[0] == ' ' || trimmed[0] == '\t')) {
        trimmed = trimmed.substr(1);
    }
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) {
        trimmed.pop_back();
    }

    // Try direct numeric parsing first
    try {
        return std::stod(trimmed);
    } catch (...) {}

    // Try to extract hazard_lambda from JSON-like string
    std::regex lambda_regex("hazard_lambda['\"]?\\s*[:=]\\s*['\"]?([0-9.]+)", std::regex::icase);
    std::smatch match;
    if (std::regex_search(params_str, match, lambda_regex)) {
        return std::stod(match[1].str());
    }
    return 250.0;  // Default
}

static unique_ptr<FunctionData> TsDetectChangepointsByBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsDetectChangepointsByBindData>();

    // Parse params from second argument
    if (input.inputs.size() >= 2) {
        string params_str = input.inputs[1].ToString();
        bind_data->hazard_lambda = ParseHazardLambda(params_str);
    }

    // Input table must have exactly 3 columns: group, date, value
    if (input.input_table_types.size() != 3) {
        throw InvalidInputException(
            "ts_detect_changepoints_by requires input with exactly 3 columns: group_col, date_col, value_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Preserve column names
    bind_data->group_col_name = input.input_table_names[0];
    bind_data->date_col_name = input.input_table_names[1];
    bind_data->value_col_name = input.input_table_names[2];
    bind_data->group_logical_type = input.input_table_types[0];
    bind_data->date_logical_type = input.input_table_types[1];

    // Detect date column type
    switch (input.input_table_types[1].id()) {
        case LogicalTypeId::DATE:
            bind_data->date_col_type = DateColumnType::DATE;
            break;
        case LogicalTypeId::TIMESTAMP:
        case LogicalTypeId::TIMESTAMP_TZ:
            bind_data->date_col_type = DateColumnType::TIMESTAMP;
            break;
        default:
            throw InvalidInputException(
                "Date column must be DATE or TIMESTAMP, got: %s",
                input.input_table_types[1].ToString().c_str());
    }

    // Output schema: group_col, date_col, is_changepoint, changepoint_probability
    names.push_back(bind_data->group_col_name);
    return_types.push_back(bind_data->group_logical_type);

    names.push_back(bind_data->date_col_name);
    return_types.push_back(bind_data->date_logical_type);

    names.push_back("is_changepoint");
    return_types.push_back(LogicalType(LogicalTypeId::BOOLEAN));

    names.push_back("changepoint_probability");
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));

    return bind_data;
}

static unique_ptr<GlobalTableFunctionState> TsDetectChangepointsByInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<TsDetectChangepointsByGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsDetectChangepointsByInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsDetectChangepointsByLocalState>();
}

static OperatorResultType TsDetectChangepointsByInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsDetectChangepointsByBindData>();
    auto &local_state = data_p.local_state->Cast<TsDetectChangepointsByLocalState>();

    // Buffer all incoming data
    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull()) continue;

        string group_key = GetGroupKey(group_val);

        if (local_state.groups.find(group_key) == local_state.groups.end()) {
            local_state.groups[group_key] = TsDetectChangepointsByLocalState::GroupData();
            local_state.groups[group_key].group_value = group_val;
            local_state.group_order.push_back(group_key);
        }

        auto &grp = local_state.groups[group_key];

        int64_t date_micros;
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                date_micros = DateToMicroseconds(date_val.GetValue<date_t>());
                break;
            case DateColumnType::TIMESTAMP:
            default:
                date_micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
                break;
        }

        grp.timestamps.push_back(date_micros);
        grp.values.push_back(value_val.IsNull() ? 0.0 : value_val.GetValue<double>());
    }

    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

static OperatorFinalizeResultType TsDetectChangepointsByFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsDetectChangepointsByBindData>();
    auto &local_state = data_p.local_state->Cast<TsDetectChangepointsByLocalState>();

    // Process all groups on first finalize call
    if (!local_state.processed) {
        for (const auto &group_key : local_state.group_order) {
            auto &grp = local_state.groups[group_key];

            if (grp.values.size() < 2) continue;

            // Sort by timestamp while preserving correspondence
            vector<pair<int64_t, double>> sorted_data;
            for (size_t i = 0; i < grp.timestamps.size(); i++) {
                sorted_data.push_back({grp.timestamps[i], grp.values[i]});
            }
            std::sort(sorted_data.begin(), sorted_data.end());

            vector<double> sorted_values;
            vector<int64_t> sorted_timestamps;
            for (const auto &p : sorted_data) {
                sorted_timestamps.push_back(p.first);
                sorted_values.push_back(p.second);
            }

            // Call Rust FFI
            BocpdResult bocpd_result = {};
            AnofoxError error = {};

            bool success = anofox_ts_detect_changepoints_bocpd(
                sorted_values.data(),
                sorted_values.size(),
                bind_data.hazard_lambda,
                true,  // Always include probabilities
                &bocpd_result,
                &error
            );

            if (!success) {
                // Skip this group on error
                continue;
            }

            // Create output rows
            for (size_t i = 0; i < bocpd_result.n_points; i++) {
                TsDetectChangepointsByLocalState::OutputRow row;
                row.group_value = grp.group_value;
                row.timestamp = sorted_timestamps[i];
                row.is_changepoint = bocpd_result.is_changepoint ? bocpd_result.is_changepoint[i] : false;
                row.changepoint_probability = bocpd_result.changepoint_probability ? bocpd_result.changepoint_probability[i] : 0.0;
                local_state.results.push_back(row);
            }

            anofox_free_bocpd_result(&bocpd_result);
        }
        local_state.processed = true;
    }

    // Output results
    if (local_state.results.empty() || local_state.output_offset >= local_state.results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t output_count = 0;

    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    while (output_count < STANDARD_VECTOR_SIZE && local_state.output_offset < local_state.results.size()) {
        auto &row = local_state.results[local_state.output_offset];
        idx_t out_idx = output_count;

        // Group column
        output.data[0].SetValue(out_idx, row.group_value);

        // Date column
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                output.data[1].SetValue(out_idx, Value::DATE(MicrosecondsToDate(row.timestamp)));
                break;
            case DateColumnType::TIMESTAMP:
            default:
                output.data[1].SetValue(out_idx, Value::TIMESTAMP(MicrosecondsToTimestamp(row.timestamp)));
                break;
        }

        // is_changepoint
        FlatVector::GetData<bool>(output.data[2])[out_idx] = row.is_changepoint;

        // changepoint_probability
        FlatVector::GetData<double>(output.data[3])[out_idx] = row.changepoint_probability;

        output_count++;
        local_state.output_offset++;
    }

    output.SetCardinality(output_count);

    if (local_state.output_offset >= local_state.results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

void RegisterTsDetectChangepointsByFunction(ExtensionLoader &loader) {
    // Internal native table function: _ts_detect_changepoints_by_native(TABLE, params_str)
    // Input table must have 3 columns: group_col, date_col, value_col
    // Called by ts_detect_changepoints_by SQL macro
    TableFunction func("_ts_detect_changepoints_by_native",
        {LogicalType::TABLE, LogicalType(LogicalTypeId::VARCHAR)},
        nullptr,
        TsDetectChangepointsByBind,
        TsDetectChangepointsByInitGlobal,
        TsDetectChangepointsByInitLocal);

    func.in_out_function = TsDetectChangepointsByInOut;
    func.in_out_function_final = TsDetectChangepointsByFinalize;

    loader.RegisterFunction(func);
}

// Note: RegisterTsDetectChangepointsAggFunction is implemented in ts_changepoints_agg.cpp

} // namespace duckdb
