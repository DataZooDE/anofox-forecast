#include "ts_cv_forecast_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For DateColumnType, helper functions
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>
#include <mutex>
#include <cmath>
#include <cstring>
#include <atomic>
#include <thread>

namespace duckdb {

// ============================================================================
// _ts_cv_forecast_native - Internal native streaming CV forecast table function
//
// This is an INTERNAL function used by ts_cv_forecast_by macro.
// Users should call ts_cv_forecast_by() instead of this function directly.
//
// REDESIGNED WORKFLOW (no frequency parameter needed):
// 1. Accepts input with BOTH train and test splits from ts_cv_folds_by
// 2. Trains model on 'train' rows per (fold_id, group) combination
// 3. Generates horizon forecasts
// 4. Matches forecasts to 'test' rows by position (1st forecast → 1st test row, etc.)
// 5. Returns test rows with forecast values
//
// Input columns: fold_id, split, group_col, date_col, target_col
// Output: All test rows with forecast, lower_90, upper_90, model_name columns added
// ============================================================================

// ============================================================================
// Bind Data - captures all parameters
// ============================================================================

struct TsCvForecastNativeBindData : public TableFunctionData {
    // Model parameters
    string method = "AutoETS";
    string model_spec = "";  // ETS model spec like "ZZZ"
    int64_t seasonal_period = 0;
    double confidence_level = 0.90;

    // Column indices (resolved by name in bind)
    idx_t fold_id_col = 0;
    idx_t split_col = 1;
    idx_t group_col = 2;
    idx_t date_col = 3;
    idx_t value_col = 4;

    // Type preservation
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
};

// ============================================================================
// Shared Data Structures
// ============================================================================

struct CvForecastGroupData {
    int64_t fold_id;
    Value group_value;
    // Training data (split='train')
    vector<int64_t> train_dates;  // microseconds
    vector<double> train_values;
    // Test data (split='test') - these have actual dates we'll match to
    vector<int64_t> test_dates;
    vector<double> test_actuals;
};

struct CvForecastOutputRow {
    int64_t fold_id;
    string group_key;
    Value group_value;
    int64_t date;  // microseconds - from actual test data
    double y;      // actual value from test data
    double forecast;
    double lower_90;
    double upper_90;
    string model_name;
};

// ============================================================================
// Global State - enables parallel execution
// ============================================================================

struct TsCvForecastNativeGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override {
        return 999999;
    }

    // Thread-safe group storage (moved from LocalState)
    std::mutex groups_mutex;
    std::map<string, CvForecastGroupData> groups;
    vector<string> group_order;

    // Processing results (used by finalize owner)
    vector<CvForecastOutputRow> results;
    bool processed = false;
    idx_t output_offset = 0;

    // Single-thread finalize + barrier
    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};
};

// ============================================================================
// Local State - buffers data per thread and manages streaming output
// ============================================================================

struct TsCvForecastNativeLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool registered_collector = false;
    bool registered_finalizer = false;
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
// Parameter Validation
// ============================================================================

static void ValidateParamKeys(const Value &params_value) {
    static const unordered_set<string> valid_keys = {
        "model", "seasonal_period", "confidence_level"
    };

    vector<string> unknown_keys;

    if (params_value.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params_value);
        for (auto &child : map_children) {
            auto &key = StructValue::GetChildren(child)[0];
            string key_str = key.ToString();
            if (valid_keys.find(key_str) == valid_keys.end()) {
                unknown_keys.push_back(key_str);
            }
        }
    } else if (params_value.type().id() == LogicalTypeId::STRUCT) {
        auto &child_types = StructType::GetChildTypes(params_value.type());
        for (idx_t i = 0; i < child_types.size(); i++) {
            if (valid_keys.find(child_types[i].first) == valid_keys.end()) {
                unknown_keys.push_back(child_types[i].first);
            }
        }
    }

    if (!unknown_keys.empty()) {
        string unknown_list;
        for (size_t i = 0; i < unknown_keys.size(); i++) {
            if (i > 0) unknown_list += ", ";
            unknown_list += "'" + unknown_keys[i] + "'";
        }
        throw InvalidInputException(
            "Unknown parameter(s): %s. Valid parameters are: model, seasonal_period, confidence_level",
            unknown_list);
    }
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

    // Input table layout (from macro):
    //   col 0: __cv_grp__   (group values)
    //   col 1: __cv_dt__    (date values)
    //   col 2: __cv_tgt__   (target as DOUBLE)
    //   col 3+: original columns from source table (including fold_id, split if present)
    auto &table_names = input.input_table_names;

    if (input.input_table_types.size() < 3) {
        throw InvalidInputException(
            "_ts_cv_forecast_native requires at least 3 input columns. Got %zu.",
            input.input_table_types.size());
    }

    // Columns 0-2 are always group/date/target
    bind_data->group_col = 0;
    bind_data->date_col = 1;
    bind_data->value_col = 2;

    // Find fold_id and split by name in remaining columns
    idx_t fold_id_idx = DConstants::INVALID_INDEX;
    idx_t split_idx = DConstants::INVALID_INDEX;
    // Also find the original group and date column names (first non-special columns after pos 2)
    string group_col_name;
    string date_col_name;

    for (idx_t i = 3; i < table_names.size(); i++) {
        if (table_names[i] == "fold_id") {
            fold_id_idx = i;
        } else if (table_names[i] == "split") {
            split_idx = i;
        }
    }

    if (fold_id_idx == DConstants::INVALID_INDEX || split_idx == DConstants::INVALID_INDEX) {
        throw InvalidInputException(
            "ts_cv_forecast_by: Input table is missing required columns 'fold_id' and/or 'split'. "
            "Create folds first:\n"
            "  CREATE TABLE folds AS SELECT * FROM ts_cv_folds_by('your_table', group, date, value, n_folds, horizon, MAP{});\n"
            "  SELECT * FROM ts_cv_forecast_by('folds', group, date, value, 'Naive', MAP{});");
    }

    bind_data->fold_id_col = fold_id_idx;
    bind_data->split_col = split_idx;

    // Find original column names (first columns after __cv_* aliases that aren't fold_id/split)
    for (idx_t i = 3; i < table_names.size(); i++) {
        if (table_names[i] == "fold_id" || table_names[i] == "split" ||
            table_names[i] == "__cv_grp__" || table_names[i] == "__cv_dt__" || table_names[i] == "__cv_tgt__") {
            continue;
        }
        if (group_col_name.empty()) {
            group_col_name = table_names[i];
        } else if (date_col_name.empty()) {
            date_col_name = table_names[i];
            break;
        }
    }
    if (group_col_name.empty()) group_col_name = "id";
    if (date_col_name.empty()) date_col_name = "date";

    // Parse method (index 1)
    if (input.inputs.size() >= 2 && !input.inputs[1].IsNull()) {
        bind_data->method = input.inputs[1].GetValue<string>();
    }

    // Parse params (index 2)
    if (input.inputs.size() >= 3 && !input.inputs[2].IsNull()) {
        auto &params = input.inputs[2];
        ValidateParamKeys(params);
        bind_data->model_spec = ParseStringFromParams(params, "model", "");
        bind_data->seasonal_period = ParseInt64FromParams(params, "seasonal_period", 0);
        bind_data->confidence_level = ParseDoubleFromParams(params, "confidence_level", 0.90);

        // Validate confidence_level range
        if (bind_data->confidence_level <= 0.0 || bind_data->confidence_level >= 1.0) {
            throw InvalidInputException(
                "Invalid confidence_level: %.2f. Must be between 0.0 and 1.0 (exclusive). "
                "Common values: 0.80 (80%%), 0.90 (90%%), 0.95 (95%%), 0.99 (99%%)",
                bind_data->confidence_level);
        }

        // Validate 'model' param is only used with ETS method
        if (!bind_data->model_spec.empty() && bind_data->method != "ETS") {
            throw InvalidInputException(
                "Parameter 'model' (value: '%s') is only valid when method='ETS'. "
                "Current method is '%s'. Remove the 'model' parameter or change method to 'ETS'.",
                bind_data->model_spec, bind_data->method);
        }
    }

    // Detect column types from input (group=col0, date=col1, value=col2)
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
                input.input_table_types[3].ToString().c_str());
    }

    // Output schema: fold_id, group_col, date_col, y, split, yhat, yhat_lower, yhat_upper, model_name
    names.push_back("fold_id");
    return_types.push_back(LogicalType::BIGINT);

    names.push_back(group_col_name);
    return_types.push_back(bind_data->group_logical_type);

    names.push_back(date_col_name);
    return_types.push_back(bind_data->date_logical_type);

    names.push_back("y");
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back("split");
    return_types.push_back(LogicalType::VARCHAR);

    names.push_back("yhat");
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back("yhat_lower");
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back("yhat_upper");
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
    return make_uniq<TsCvForecastNativeGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsCvForecastNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsCvForecastNativeLocalState>();
}

// ============================================================================
// In-Out Function - buffers incoming data, separates train and test
// ============================================================================

static OperatorResultType TsCvForecastNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsCvForecastNativeBindData>();
    auto &lstate = data_p.local_state->Cast<TsCvForecastNativeLocalState>();
    auto &gstate = data_p.global_state->Cast<TsCvForecastNativeGlobalState>();

    // Register this thread as a collector
    if (!lstate.registered_collector) {
        gstate.threads_collecting.fetch_add(1);
        lstate.registered_collector = true;
    }

    // Buffer all incoming data - we need complete groups
    // Input columns are at indices stored in bind_data
    // Extract batch locally first, then lock once
    struct LocalRow {
        string composite_key;
        int64_t fold_id;
        Value group_val;
        string split;
        int64_t date_micros;
        double value;
    };
    vector<LocalRow> local_rows;
    local_rows.reserve(input.size());

    for (idx_t i = 0; i < input.size(); i++) {
        Value fold_id_val = input.data[bind_data.fold_id_col].GetValue(i);
        Value split_val = input.data[bind_data.split_col].GetValue(i);
        Value group_val = input.data[bind_data.group_col].GetValue(i);
        Value date_val = input.data[bind_data.date_col].GetValue(i);
        Value value_val = input.data[bind_data.value_col].GetValue(i);

        if (fold_id_val.IsNull() || split_val.IsNull() || date_val.IsNull()) continue;

        int64_t fold_id = fold_id_val.GetValue<int64_t>();
        string split = split_val.ToString();
        string group_key = GetGroupKey(group_val);
        string composite_key = MakeCompositeKey(fold_id, group_key);

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

        double value = value_val.IsNull() ? 0.0 : value_val.GetValue<double>();

        local_rows.push_back({std::move(composite_key), fold_id, group_val, std::move(split), date_micros, value});
    }

    // Lock once and insert all rows into global state
    {
        std::lock_guard<std::mutex> lock(gstate.groups_mutex);
        for (auto &row : local_rows) {
            if (gstate.groups.find(row.composite_key) == gstate.groups.end()) {
                gstate.groups[row.composite_key] = CvForecastGroupData();
                gstate.groups[row.composite_key].fold_id = row.fold_id;
                gstate.groups[row.composite_key].group_value = row.group_val;
                gstate.group_order.push_back(row.composite_key);
            }

            auto &grp = gstate.groups[row.composite_key];

            // Separate train and test data
            if (row.split == "train") {
                grp.train_dates.push_back(row.date_micros);
                grp.train_values.push_back(row.value);
            } else if (row.split == "test") {
                grp.test_dates.push_back(row.date_micros);
                grp.test_actuals.push_back(row.value);
            }
            // Ignore other split values
        }
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
    auto &gstate = data_p.global_state->Cast<TsCvForecastNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsCvForecastNativeLocalState>();

    // Barrier + claim pattern: ensure all collectors are done before processing
    if (!lstate.registered_finalizer) {
        if (lstate.registered_collector)
            gstate.threads_done_collecting.fetch_add(1);
        lstate.registered_finalizer = true;
    }
    if (!lstate.owns_finalize) {
        bool expected = false;
        if (!gstate.finalize_claimed.compare_exchange_strong(expected, true))
            return OperatorFinalizeResultType::FINISHED;
        lstate.owns_finalize = true;
        while (gstate.threads_done_collecting.load() < gstate.threads_collecting.load())
            std::this_thread::yield();
    }

    // Process all groups on first finalize call
    if (!gstate.processed) {
        for (const auto &composite_key : gstate.group_order) {
            auto &grp = gstate.groups[composite_key];

            if (grp.train_dates.empty() || grp.test_dates.empty()) continue;

            // Sort training data by date
            vector<size_t> train_indices(grp.train_dates.size());
            for (size_t i = 0; i < train_indices.size(); i++) train_indices[i] = i;
            std::sort(train_indices.begin(), train_indices.end(),
                [&grp](size_t a, size_t b) { return grp.train_dates[a] < grp.train_dates[b]; });

            vector<double> sorted_train_values(grp.train_values.size());
            for (size_t i = 0; i < train_indices.size(); i++) {
                sorted_train_values[i] = grp.train_values[train_indices[i]];
            }

            // Sort test data by date
            vector<size_t> test_indices(grp.test_dates.size());
            for (size_t i = 0; i < test_indices.size(); i++) test_indices[i] = i;
            std::sort(test_indices.begin(), test_indices.end(),
                [&grp](size_t a, size_t b) { return grp.test_dates[a] < grp.test_dates[b]; });

            vector<int64_t> sorted_test_dates(grp.test_dates.size());
            vector<double> sorted_test_actuals(grp.test_actuals.size());
            for (size_t i = 0; i < test_indices.size(); i++) {
                sorted_test_dates[i] = grp.test_dates[test_indices[i]];
                sorted_test_actuals[i] = grp.test_actuals[test_indices[i]];
            }

            // Build ForecastOptions
            ForecastOptions opts;
            memset(&opts, 0, sizeof(opts));

            strncpy(opts.model, bind_data.method.c_str(), sizeof(opts.model) - 1);
            opts.model[sizeof(opts.model) - 1] = '\0';
            if (!bind_data.model_spec.empty()) {
                strncpy(opts.ets_model, bind_data.model_spec.c_str(), sizeof(opts.ets_model) - 1);
                opts.ets_model[sizeof(opts.ets_model) - 1] = '\0';
            }

            // Horizon is inferred from test data size
            opts.horizon = static_cast<int>(sorted_test_dates.size());
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
                sorted_train_values.data(),
                nullptr,  // No validity mask
                sorted_train_values.size(),
                &opts,
                &fcst_result,
                &error
            );

            if (!success) {
                if (error.code == INVALID_MODEL || error.code == INVALID_INPUT) {
                    throw InvalidInputException(string(error.message));
                }
                // Skip this group on computation/data errors
                continue;
            }

            // Match forecasts to test rows by position
            // forecast[0] -> test row 0, forecast[1] -> test row 1, etc.
            size_t n_matches = std::min(static_cast<size_t>(fcst_result.n_forecasts), sorted_test_dates.size());
            for (size_t i = 0; i < n_matches; i++) {
                CvForecastOutputRow row;
                row.fold_id = grp.fold_id;
                row.group_key = composite_key;
                row.group_value = grp.group_value;
                row.date = sorted_test_dates[i];  // Use actual test date
                row.y = sorted_test_actuals[i];   // Actual value
                row.forecast = fcst_result.point_forecasts[i];
                row.lower_90 = fcst_result.lower_bounds[i];
                row.upper_90 = fcst_result.upper_bounds[i];
                row.model_name = string(fcst_result.model_name);

                gstate.results.push_back(row);
            }

            // Free Rust-allocated memory
            anofox_free_forecast_result(&fcst_result);
        }

        gstate.processed = true;
    }

    // Output results in batches
    idx_t remaining = gstate.results.size() - gstate.output_offset;
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
        auto &row = gstate.results[gstate.output_offset + i];

        // fold_id
        output.data[0].SetValue(i, Value::BIGINT(row.fold_id));

        // group_col
        output.data[1].SetValue(i, row.group_value);

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

        // y (actual)
        output.data[3].SetValue(i, Value::DOUBLE(row.y));

        // split (always 'test' since we only output test rows)
        output.data[4].SetValue(i, Value("test"));

        // forecast, lower_90, upper_90
        output.data[5].SetValue(i, Value::DOUBLE(row.forecast));
        output.data[6].SetValue(i, Value::DOUBLE(row.lower_90));
        output.data[7].SetValue(i, Value::DOUBLE(row.upper_90));

        // model_name
        output.data[8].SetValue(i, Value(row.model_name));
    }

    gstate.output_offset += to_output;

    if (gstate.output_offset >= gstate.results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }
    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsCvForecastNativeFunction(ExtensionLoader &loader) {
    // Internal table-in-out function: (TABLE, method, params)
    // Input table must have 5 columns: fold_id, split, group_col, date_col, value_col
    //
    // This is an internal function (prefixed with _) called by ts_cv_forecast_by macro.
    // Horizon is inferred from the number of test rows per fold/group.
    TableFunction func("_ts_cv_forecast_native",
        {LogicalType::TABLE, LogicalType::VARCHAR, LogicalType::ANY},
        nullptr,  // No execute function - use in_out_function
        TsCvForecastNativeBind,
        TsCvForecastNativeInitGlobal,
        TsCvForecastNativeInitLocal);

    func.in_out_function = TsCvForecastNativeInOut;
    func.in_out_function_final = TsCvForecastNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
