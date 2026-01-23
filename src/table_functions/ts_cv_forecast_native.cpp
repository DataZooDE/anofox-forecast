#include "ts_cv_forecast_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For ParseFrequencyToSeconds, etc.
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <cmath>
#include <cstring>

namespace duckdb {

// ============================================================================
// _ts_cv_forecast_native - Internal native streaming CV forecast table function
//
// This is an INTERNAL function used by ts_cv_forecast_by macro.
// Users should call ts_cv_forecast_by() instead of this function directly.
//
// MEMORY FOOTPRINT:
//   - Native (this function): O(group_size) per (fold_id, group) combination
//   - Old SQL macro approach: O(total_rows) due to LIST() aggregations
//
// Input columns: fold_id, group_col, date_col, target_col
// Groups by (fold_id, group_col) and generates forecasts for each combination.
// ============================================================================

// ============================================================================
// Bind Data - captures all parameters
// ============================================================================

struct TsCvForecastNativeBindData : public TableFunctionData {
    // Required parameters
    int64_t horizon = 7;
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;

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
// Local State - buffers data per thread and manages streaming output
// ============================================================================

struct TsCvForecastNativeLocalState : public LocalTableFunctionState {
    // Input data buffer per (fold_id, group) combination
    struct GroupData {
        int64_t fold_id;
        Value group_value;
        vector<int64_t> dates;  // microseconds
        vector<double> values;
        vector<bool> validity;
    };

    // Key is "fold_id:group_key"
    std::map<string, GroupData> groups;
    vector<string> group_order;

    // Output results
    struct ForecastOutputRow {
        int64_t fold_id;
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

// Build composite key from fold_id and group value
static string MakeCompositeKey(int64_t fold_id, const string &group_key) {
    return std::to_string(fold_id) + ":" + group_key;
}

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsCvForecastNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsCvForecastNativeBindData>();

    // Input table has columns: fold_id, group_col, date_col, value_col
    // Arguments after table: horizon, frequency, method, params

    // Parse horizon (index 1)
    if (input.inputs.size() >= 2) {
        bind_data->horizon = input.inputs[1].GetValue<int64_t>();
    }

    // Parse frequency (index 2)
    if (input.inputs.size() >= 3) {
        string freq_str = input.inputs[2].GetValue<string>();
        auto [freq, is_raw] = ParseFrequencyToSeconds(freq_str);
        bind_data->frequency_seconds = freq;
        bind_data->frequency_is_raw = is_raw;
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
    // Input table: fold_id (BIGINT), group_col, date_col, value_col
    bind_data->group_logical_type = input.input_table_types[1];
    bind_data->date_logical_type = input.input_table_types[2];

    switch (input.input_table_types[2].id()) {
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
                input.input_table_types[2].ToString().c_str());
    }

    // Output schema: fold_id, id, forecast_step, date, point_forecast, lower_90, upper_90, model_name
    names.push_back("fold_id");
    return_types.push_back(LogicalType::BIGINT);

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

static unique_ptr<GlobalTableFunctionState> TsCvForecastNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<GlobalTableFunctionState>();
}

static unique_ptr<LocalTableFunctionState> TsCvForecastNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsCvForecastNativeLocalState>();
}

// ============================================================================
// In-Out Function - buffers incoming data
// ============================================================================

static OperatorResultType TsCvForecastNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsCvForecastNativeBindData>();
    auto &local_state = data_p.local_state->Cast<TsCvForecastNativeLocalState>();

    // Buffer all incoming data - we need complete groups
    // Input columns: fold_id, group_col, date_col, value_col
    for (idx_t i = 0; i < input.size(); i++) {
        Value fold_id_val = input.data[0].GetValue(i);
        Value group_val = input.data[1].GetValue(i);
        Value date_val = input.data[2].GetValue(i);
        Value value_val = input.data[3].GetValue(i);

        if (fold_id_val.IsNull() || date_val.IsNull()) continue;

        int64_t fold_id = fold_id_val.GetValue<int64_t>();
        string group_key = GetGroupKey(group_val);
        string composite_key = MakeCompositeKey(fold_id, group_key);

        if (local_state.groups.find(composite_key) == local_state.groups.end()) {
            local_state.groups[composite_key] = TsCvForecastNativeLocalState::GroupData();
            local_state.groups[composite_key].fold_id = fold_id;
            local_state.groups[composite_key].group_value = group_val;
            local_state.group_order.push_back(composite_key);
        }

        auto &grp = local_state.groups[composite_key];

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

static OperatorFinalizeResultType TsCvForecastNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsCvForecastNativeBindData>();
    auto &local_state = data_p.local_state->Cast<TsCvForecastNativeLocalState>();

    // Process all groups on first finalize call
    if (!local_state.processed) {
        for (const auto &composite_key : local_state.group_order) {
            auto &grp = local_state.groups[composite_key];

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

            // Compute forecast dates
            int64_t freq_micros;
            if (bind_data.date_col_type == DateColumnType::INTEGER ||
                bind_data.date_col_type == DateColumnType::BIGINT) {
                freq_micros = bind_data.frequency_is_raw ? bind_data.frequency_seconds : bind_data.frequency_seconds;
            } else {
                freq_micros = bind_data.frequency_is_raw
                    ? bind_data.frequency_seconds * 86400LL * 1000000LL
                    : bind_data.frequency_seconds * 1000000LL;
            }

            // Generate output rows
            for (size_t i = 0; i < fcst_result.n_forecasts; i++) {
                TsCvForecastNativeLocalState::ForecastOutputRow row;
                row.fold_id = grp.fold_id;
                row.group_key = composite_key;
                row.group_value = grp.group_value;
                row.forecast_step = static_cast<int64_t>(i + 1);
                row.date = last_date + freq_micros * static_cast<int64_t>(i + 1);
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

    for (idx_t i = 0; i < to_output; i++) {
        auto &row = local_state.results[local_state.output_offset + i];

        // fold_id
        output.data[0].SetValue(i, Value::BIGINT(row.fold_id));

        // id (group)
        output.data[1].SetValue(i, row.group_value);

        // forecast_step
        output.data[2].SetValue(i, Value::INTEGER(static_cast<int32_t>(row.forecast_step)));

        // date - convert back from microseconds
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                output.data[3].SetValue(i, Value::DATE(MicrosecondsToDate(row.date)));
                break;
            case DateColumnType::TIMESTAMP:
                output.data[3].SetValue(i, Value::TIMESTAMP(MicrosecondsToTimestamp(row.date)));
                break;
            case DateColumnType::INTEGER:
                output.data[3].SetValue(i, Value::INTEGER(static_cast<int32_t>(row.date)));
                break;
            case DateColumnType::BIGINT:
                output.data[3].SetValue(i, Value::BIGINT(row.date));
                break;
        }

        // point_forecast, lower_90, upper_90
        output.data[4].SetValue(i, Value::DOUBLE(row.point_forecast));
        output.data[5].SetValue(i, Value::DOUBLE(row.lower_90));
        output.data[6].SetValue(i, Value::DOUBLE(row.upper_90));

        // model_name
        output.data[7].SetValue(i, Value(row.model_name));
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

void RegisterTsCvForecastNativeFunction(ExtensionLoader &loader) {
    // Internal table-in-out function: (TABLE, horizon, frequency, method, params)
    // Input table must have 4 columns: fold_id, group_col, date_col, value_col
    // Note: This is an internal function (prefixed with _) called by ts_cv_forecast_by macro
    TableFunction func("_ts_cv_forecast_native",
        {LogicalType::TABLE, LogicalType::INTEGER, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::ANY},
        nullptr,  // No execute function - use in_out_function
        TsCvForecastNativeBind,
        TsCvForecastNativeInitGlobal,
        TsCvForecastNativeInitLocal);

    func.in_out_function = TsCvForecastNativeInOut;
    func.in_out_function_final = TsCvForecastNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
