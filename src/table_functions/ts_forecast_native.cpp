#include "ts_forecast_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For ParseFrequencyToSeconds, etc.
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <mutex>
#include <cmath>
#include <cstring>

namespace duckdb {

// ============================================================================
// _ts_forecast_native - Internal native streaming forecast table function
//
// This is an INTERNAL function used by ts_forecast_by macro.
// Users should call ts_forecast_by() instead of this function directly.
//
// MEMORY FOOTPRINT:
//   - Native (this function): O(group_size) per group
//   - Old SQL macro approach: O(total_rows) due to LIST() aggregations
//
// ============================================================================

// ============================================================================
// Bind Data - captures all parameters
// ============================================================================

struct TsForecastNativeBindData : public TableFunctionData {
    // Required parameters
    int64_t horizon = 7;
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;
    FrequencyType frequency_type = FrequencyType::FIXED;  // Calendar frequency support

    // Model parameters
    string method = "AutoETS";
    string model_spec = "";  // ETS model spec like "ZZZ"
    int64_t seasonal_period = 0;
    double confidence_level = 0.90;

    // Type preservation
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
};

// ============================================================================
// Global State - enables parallel execution
//
// IMPORTANT: This custom GlobalState is required for proper parallel execution.
// Using the base GlobalTableFunctionState directly causes batch index collisions
// with large datasets (300k+ groups) during BatchedDataCollection::Merge.
// ============================================================================

struct TsForecastNativeGlobalState : public GlobalTableFunctionState {
    // Allow parallel execution - each thread processes its partition of groups
    // DuckDB assigns unique batch indices per thread when we properly declare
    // parallel support via this MaxThreads override.
    idx_t MaxThreads() const override {
        return 999999;  // Unlimited - let DuckDB decide based on hardware
    }

    // Global group tracking to prevent duplicate processing
    // When DuckDB partitions input, the same group may be sent to multiple threads.
    // We use this set to ensure each group is only processed once.
    std::mutex processed_groups_mutex;
    std::set<string> processed_groups;

    // Try to claim a group for processing. Returns true if this thread should process it.
    bool ClaimGroup(const string &group_key) {
        std::lock_guard<std::mutex> lock(processed_groups_mutex);
        auto result = processed_groups.insert(group_key);
        return result.second;  // true if insertion happened (group was not already claimed)
    }
};

// ============================================================================
// Local State - buffers data per thread and manages streaming output
// ============================================================================

struct TsForecastNativeLocalState : public LocalTableFunctionState {
    // Input data buffer per group
    struct GroupData {
        Value group_value;
        vector<int64_t> dates;  // microseconds
        vector<double> values;
        vector<bool> validity;
    };

    std::map<string, GroupData> groups;
    vector<string> group_order;

    // Output results
    struct ForecastOutputRow {
        string group_key;
        Value group_value;
        int64_t forecast_step;
        int64_t date;  // microseconds
        double point_forecast;
        double lower_90;
        double upper_90;
        string model_name;
    };
    vector<ForecastOutputRow> results;

    // Processing state
    bool processed = false;
    idx_t output_offset = 0;
};

// ============================================================================
// Helper Functions
// ============================================================================

static string ParseMethodFromParams(const Value &params_value) {
    if (params_value.IsNull()) {
        return "AutoETS";
    }

    // Handle MAP type
    if (params_value.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params_value);
        for (auto &child : map_children) {
            auto &key = StructValue::GetChildren(child)[0];
            auto &val = StructValue::GetChildren(child)[1];
            if (key.ToString() == "method" && !val.IsNull()) {
                return val.ToString();
            }
        }
    }
    // Handle STRUCT type
    else if (params_value.type().id() == LogicalTypeId::STRUCT) {
        auto &struct_children = StructValue::GetChildren(params_value);
        auto &child_types = StructType::GetChildTypes(params_value.type());
        for (idx_t i = 0; i < child_types.size(); i++) {
            if (child_types[i].first == "method" && !struct_children[i].IsNull()) {
                return struct_children[i].ToString();
            }
        }
    }

    return "AutoETS";
}

static string ParseStringFromParams(const Value &params_value, const string &key, const string &default_val) {
    if (params_value.IsNull()) {
        return default_val;
    }

    // Handle MAP type
    if (params_value.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params_value);
        for (auto &child : map_children) {
            auto &k = StructValue::GetChildren(child)[0];
            auto &v = StructValue::GetChildren(child)[1];
            if (k.ToString() == key && !v.IsNull()) {
                return v.ToString();
            }
        }
    }
    // Handle STRUCT type
    else if (params_value.type().id() == LogicalTypeId::STRUCT) {
        auto &struct_children = StructValue::GetChildren(params_value);
        auto &child_types = StructType::GetChildTypes(params_value.type());
        for (idx_t i = 0; i < child_types.size(); i++) {
            if (child_types[i].first == key && !struct_children[i].IsNull()) {
                return struct_children[i].ToString();
            }
        }
    }

    return default_val;
}

static int64_t ParseInt64FromParams(const Value &params_value, const string &key, int64_t default_val) {
    if (params_value.IsNull()) {
        return default_val;
    }

    // Handle MAP type
    if (params_value.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params_value);
        for (auto &child : map_children) {
            auto &k = StructValue::GetChildren(child)[0];
            auto &v = StructValue::GetChildren(child)[1];
            if (k.ToString() == key && !v.IsNull()) {
                try {
                    return std::stoll(v.ToString());
                } catch (...) {
                    return default_val;
                }
            }
        }
    }
    // Handle STRUCT type
    else if (params_value.type().id() == LogicalTypeId::STRUCT) {
        auto &struct_children = StructValue::GetChildren(params_value);
        auto &child_types = StructType::GetChildTypes(params_value.type());
        for (idx_t i = 0; i < child_types.size(); i++) {
            if (child_types[i].first == key && !struct_children[i].IsNull()) {
                try {
                    return struct_children[i].GetValue<int64_t>();
                } catch (...) {
                    try {
                        return std::stoll(struct_children[i].ToString());
                    } catch (...) {
                        return default_val;
                    }
                }
            }
        }
    }

    return default_val;
}

static double ParseDoubleFromParams(const Value &params_value, const string &key, double default_val) {
    if (params_value.IsNull()) {
        return default_val;
    }

    // Handle MAP type
    if (params_value.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params_value);
        for (auto &child : map_children) {
            auto &k = StructValue::GetChildren(child)[0];
            auto &v = StructValue::GetChildren(child)[1];
            if (k.ToString() == key && !v.IsNull()) {
                try {
                    return std::stod(v.ToString());
                } catch (...) {
                    return default_val;
                }
            }
        }
    }
    // Handle STRUCT type
    else if (params_value.type().id() == LogicalTypeId::STRUCT) {
        auto &struct_children = StructValue::GetChildren(params_value);
        auto &child_types = StructType::GetChildTypes(params_value.type());
        for (idx_t i = 0; i < child_types.size(); i++) {
            if (child_types[i].first == key && !struct_children[i].IsNull()) {
                try {
                    return struct_children[i].GetValue<double>();
                } catch (...) {
                    try {
                        return std::stod(struct_children[i].ToString());
                    } catch (...) {
                        return default_val;
                    }
                }
            }
        }
    }

    return default_val;
}

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsForecastNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsForecastNativeBindData>();

    // Input table has columns: group_col, date_col, value_col
    // Arguments after table: horizon, frequency, params

    // Parse horizon (index 1)
    if (input.inputs.size() >= 2) {
        bind_data->horizon = input.inputs[1].GetValue<int64_t>();
    }

    // Parse frequency (index 2) - supports calendar frequencies (monthly, quarterly, yearly)
    if (input.inputs.size() >= 3) {
        string freq_str = input.inputs[2].GetValue<string>();
        auto parsed = ParseFrequencyWithType(freq_str);
        bind_data->frequency_seconds = parsed.seconds;
        bind_data->frequency_is_raw = parsed.is_raw;
        bind_data->frequency_type = parsed.type;
    }

    // Parse method (index 3)
    if (input.inputs.size() >= 4 && !input.inputs[3].IsNull()) {
        bind_data->method = input.inputs[3].GetValue<string>();
    }

    // Parse params (index 4)
    if (input.inputs.size() >= 5 && !input.inputs[4].IsNull()) {
        auto &params = input.inputs[4];
        bind_data->model_spec = ParseStringFromParams(params, "model", "");
        bind_data->seasonal_period = ParseInt64FromParams(params, "seasonal_period", 0);
        bind_data->confidence_level = ParseDoubleFromParams(params, "confidence_level", 0.90);
    }

    // Detect column types from input
    bind_data->group_logical_type = input.input_table_types[0];
    bind_data->date_logical_type = input.input_table_types[1];

    switch (input.input_table_types[1].id()) {
        case LogicalTypeId::DATE:
            bind_data->date_col_type = DateColumnType::DATE;
            break;
        case LogicalTypeId::TIMESTAMP:
        case LogicalTypeId::TIMESTAMP_TZ:
            bind_data->date_col_type = DateColumnType::TIMESTAMP;
            break;
        case LogicalTypeId::INTEGER:
            bind_data->date_col_type = DateColumnType::INTEGER;
            break;
        case LogicalTypeId::BIGINT:
            bind_data->date_col_type = DateColumnType::BIGINT;
            break;
        default:
            throw InvalidInputException(
                "Date column must be DATE, TIMESTAMP, INTEGER, or BIGINT, got: %s",
                input.input_table_types[1].ToString().c_str());
    }

    // Output schema: id, forecast_step, date, point_forecast, lower_90, upper_90, model_name
    names.push_back("id");
    return_types.push_back(bind_data->group_logical_type);

    names.push_back("forecast_step");
    return_types.push_back(LogicalType::INTEGER);

    names.push_back("date");
    return_types.push_back(bind_data->date_logical_type);

    names.push_back("point_forecast");
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back("lower_90");
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back("upper_90");
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back("model_name");
    return_types.push_back(LogicalType::VARCHAR);

    return bind_data;
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsForecastNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<TsForecastNativeGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsForecastNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsForecastNativeLocalState>();
}

// ============================================================================
// In-Out Function - buffers incoming data
// ============================================================================

static OperatorResultType TsForecastNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsForecastNativeBindData>();
    auto &local_state = data_p.local_state->Cast<TsForecastNativeLocalState>();

    // Buffer all incoming data - we need complete groups
    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull()) continue;

        string group_key = GetGroupKey(group_val);

        if (local_state.groups.find(group_key) == local_state.groups.end()) {
            local_state.groups[group_key] = TsForecastNativeLocalState::GroupData();
            local_state.groups[group_key].group_value = group_val;
            local_state.group_order.push_back(group_key);
        }

        auto &grp = local_state.groups[group_key];

        // Convert date to microseconds
        int64_t date_micros;
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                date_micros = DateToMicroseconds(date_val.GetValue<date_t>());
                break;
            case DateColumnType::TIMESTAMP:
                date_micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
                break;
            case DateColumnType::INTEGER:
                date_micros = date_val.GetValue<int32_t>();
                break;
            case DateColumnType::BIGINT:
                date_micros = date_val.GetValue<int64_t>();
                break;
        }

        grp.dates.push_back(date_micros);
        grp.values.push_back(value_val.IsNull() ? 0.0 : value_val.GetValue<double>());
        grp.validity.push_back(!value_val.IsNull());
    }

    // Don't output anything during input phase
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - process accumulated data and output results
// ============================================================================

static OperatorFinalizeResultType TsForecastNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsForecastNativeBindData>();
    auto &global_state = data_p.global_state->Cast<TsForecastNativeGlobalState>();
    auto &local_state = data_p.local_state->Cast<TsForecastNativeLocalState>();

    // Process all groups on first finalize call
    if (!local_state.processed) {
        for (const auto &group_key : local_state.group_order) {
            // Skip if another thread already claimed this group
            if (!global_state.ClaimGroup(group_key)) {
                continue;
            }

            auto &grp = local_state.groups[group_key];

            if (grp.dates.empty()) continue;

            // Sort by date
            vector<size_t> indices(grp.dates.size());
            for (size_t i = 0; i < indices.size(); i++) indices[i] = i;
            std::sort(indices.begin(), indices.end(),
                [&grp](size_t a, size_t b) { return grp.dates[a] < grp.dates[b]; });

            vector<double> sorted_values(grp.values.size());
            vector<bool> sorted_validity(grp.validity.size());
            int64_t last_date = 0;
            for (size_t i = 0; i < indices.size(); i++) {
                sorted_values[i] = grp.values[indices[i]];
                sorted_validity[i] = grp.validity[indices[i]];
                last_date = grp.dates[indices[i]];
            }

            // Build validity bitmask for Rust
            size_t validity_words = (sorted_values.size() + 63) / 64;
            vector<uint64_t> validity(validity_words, 0);
            for (size_t i = 0; i < sorted_validity.size(); i++) {
                if (sorted_validity[i]) {
                    validity[i / 64] |= (1ULL << (i % 64));
                }
            }

            // Build ForecastOptions
            ForecastOptions opts;
            memset(&opts, 0, sizeof(opts));

            // Combine method and model_spec
            string full_method = bind_data.method;
            if (!bind_data.model_spec.empty()) {
                full_method += ":" + bind_data.model_spec;
            }
            strncpy(opts.model, full_method.c_str(), sizeof(opts.model) - 1);
            opts.model[sizeof(opts.model) - 1] = '\0';

            opts.horizon = static_cast<int>(bind_data.horizon);
            opts.confidence_level = bind_data.confidence_level;
            opts.seasonal_period = static_cast<int>(bind_data.seasonal_period);
            opts.auto_detect_seasonality = (bind_data.seasonal_period == 0);
            opts.include_fitted = false;
            opts.include_residuals = false;

            // Call Rust FFI
            ForecastResult fcst_result;
            memset(&fcst_result, 0, sizeof(fcst_result));
            AnofoxError error;

            bool success = anofox_ts_forecast(
                sorted_values.data(),
                validity.empty() ? nullptr : validity.data(),
                sorted_values.size(),
                &opts,
                &fcst_result,
                &error
            );

            if (!success) {
                // Skip this group on error
                continue;
            }

            // Generate output rows with calendar-aware date arithmetic
            for (size_t i = 0; i < fcst_result.n_forecasts; i++) {
                TsForecastNativeLocalState::ForecastOutputRow row;
                row.group_key = group_key;
                row.group_value = grp.group_value;
                row.forecast_step = static_cast<int64_t>(i + 1);

                // Compute forecast date based on frequency type
                int64_t steps = static_cast<int64_t>(i + 1);
                if (bind_data.frequency_type == FrequencyType::MONTHLY ||
                    bind_data.frequency_type == FrequencyType::QUARTERLY ||
                    bind_data.frequency_type == FrequencyType::YEARLY) {
                    // Calendar-aware date arithmetic for monthly/quarterly/yearly
                    date_t base_date = MicrosecondsToDate(last_date);
                    int32_t year, month, day;
                    Date::Convert(base_date, year, month, day);

                    // Calculate months to add
                    int64_t months_to_add = steps * bind_data.frequency_seconds;
                    if (bind_data.frequency_type == FrequencyType::QUARTERLY) {
                        months_to_add *= 3;
                    } else if (bind_data.frequency_type == FrequencyType::YEARLY) {
                        months_to_add *= 12;
                    }

                    // Add months with proper year/month rollover
                    int64_t total_months = static_cast<int64_t>(year) * 12 + (month - 1) + months_to_add;
                    int32_t new_year = static_cast<int32_t>(total_months / 12);
                    int32_t new_month = static_cast<int32_t>((total_months % 12) + 1);

                    // Handle month overflow for months < 1
                    if (new_month < 1) {
                        new_month += 12;
                        new_year -= 1;
                    }

                    // Clamp day to valid range for the new month
                    int32_t max_day = Date::MonthDays(new_year, new_month);
                    int32_t new_day = std::min(day, max_day);

                    date_t new_date = Date::FromDate(new_year, new_month, new_day);
                    row.date = DateToMicroseconds(new_date);
                } else {
                    // Fixed frequency (days, hours, etc.) - simple arithmetic
                    int64_t freq_micros;
                    if (bind_data.date_col_type == DateColumnType::INTEGER ||
                        bind_data.date_col_type == DateColumnType::BIGINT) {
                        freq_micros = bind_data.frequency_seconds;
                    } else {
                        freq_micros = bind_data.frequency_is_raw
                            ? bind_data.frequency_seconds * 86400LL * 1000000LL
                            : bind_data.frequency_seconds * 1000000LL;
                    }
                    row.date = last_date + freq_micros * steps;
                }

                row.point_forecast = fcst_result.point_forecasts[i];
                row.lower_90 = fcst_result.lower_bounds[i];
                row.upper_90 = fcst_result.upper_bounds[i];
                row.model_name = string(fcst_result.model_name);

                local_state.results.push_back(row);
            }

            // Free Rust-allocated memory
            anofox_free_forecast_result(&fcst_result);
        }

        local_state.processed = true;
    }

    // Output results in batches
    idx_t remaining = local_state.results.size() - local_state.output_offset;
    if (remaining == 0) {
        output.SetCardinality(0);
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t to_output = std::min(remaining, static_cast<idx_t>(STANDARD_VECTOR_SIZE));
    output.SetCardinality(to_output);

    // Initialize all output vectors as FLAT_VECTOR for parallel-safe batch merging
    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    for (idx_t i = 0; i < to_output; i++) {
        auto &row = local_state.results[local_state.output_offset + i];

        // id (group)
        output.data[0].SetValue(i, row.group_value);

        // forecast_step
        output.data[1].SetValue(i, Value::INTEGER(static_cast<int32_t>(row.forecast_step)));

        // date - convert back from microseconds
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                output.data[2].SetValue(i, Value::DATE(MicrosecondsToDate(row.date)));
                break;
            case DateColumnType::TIMESTAMP:
                output.data[2].SetValue(i, Value::TIMESTAMP(MicrosecondsToTimestamp(row.date)));
                break;
            case DateColumnType::INTEGER:
                output.data[2].SetValue(i, Value::INTEGER(static_cast<int32_t>(row.date)));
                break;
            case DateColumnType::BIGINT:
                output.data[2].SetValue(i, Value::BIGINT(row.date));
                break;
        }

        // point_forecast, lower_90, upper_90
        output.data[3].SetValue(i, Value::DOUBLE(row.point_forecast));
        output.data[4].SetValue(i, Value::DOUBLE(row.lower_90));
        output.data[5].SetValue(i, Value::DOUBLE(row.upper_90));

        // model_name
        output.data[6].SetValue(i, Value(row.model_name));
    }

    local_state.output_offset += to_output;

    if (local_state.output_offset >= local_state.results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }
    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsForecastNativeFunction(ExtensionLoader &loader) {
    // Internal table-in-out function: (TABLE, horizon, frequency, method, params)
    // Input table must have 3 columns: group_col, date_col, value_col
    // Note: This is an internal function (prefixed with _) called by ts_forecast_by macro
    TableFunction func("_ts_forecast_native",
        {LogicalType::TABLE, LogicalType::INTEGER, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::ANY},
        nullptr,  // No execute function - use in_out_function
        TsForecastNativeBind,
        TsForecastNativeInitGlobal,
        TsForecastNativeInitLocal);

    func.in_out_function = TsForecastNativeInOut;
    func.in_out_function_final = TsForecastNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
