#include "ts_backtest_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For ParseFrequencyToSeconds, etc.
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <cmath>
#include <cstring>
#include <set>

namespace duckdb {

// ============================================================================
// Bind Data - captures all parameters
// ============================================================================

struct TsBacktestNativeBindData : public TableFunctionData {
    // Required parameters
    int64_t horizon = 7;
    int64_t folds = 5;
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;

    // Optional parameters (from params MAP/STRUCT)
    string method = "AutoETS";
    string model_spec = "";  // ETS model spec like "ZZZ"
    string window_type = "expanding";
    int64_t min_train_size = 1;
    int64_t gap = 0;
    int64_t embargo = 0;
    int64_t initial_train_size = -1;  // -1 means auto (n_dates/2)
    int64_t skip_length = -1;         // -1 means horizon
    bool clip_horizon = false;

    // Metric for fold scores
    string metric = "rmse";

    // Type preservation
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
};

// ============================================================================
// Local State - buffers data and manages streaming output
// ============================================================================

struct TsBacktestNativeLocalState : public LocalTableFunctionState {
    // Input data buffer per group
    struct GroupData {
        Value group_value;
        vector<int64_t> dates;  // microseconds
        vector<double> values;
    };

    std::map<string, GroupData> groups;
    vector<string> group_order;

    // Fold boundaries (computed once)
    struct FoldBoundary {
        int64_t fold_id;
        int64_t train_start;  // microseconds
        int64_t train_end;
        int64_t test_start;
        int64_t test_end;
    };
    vector<FoldBoundary> fold_bounds;

    // Output results
    struct BacktestResult {
        int64_t fold_id;
        string group_key;
        Value group_value;
        int64_t date;  // microseconds
        double forecast;
        double actual;
        double error;
        double abs_error;
        double lower_90;
        double upper_90;
        string model_name;
        double fold_metric_score;
    };
    vector<BacktestResult> results;

    // Processing state
    bool folds_computed = false;
    idx_t current_fold = 0;
    idx_t current_group = 0;
    idx_t output_offset = 0;
    bool finished = false;
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

static bool ParseBoolFromParams(const Value &params_value, const string &key, bool default_val) {
    if (params_value.IsNull()) {
        return default_val;
    }

    string str_val = ParseStringFromParams(params_value, key, default_val ? "true" : "false");
    string lower = StringUtil::Lower(str_val);
    return lower == "true" || lower == "1" || lower == "yes";
}

static double ComputeMetric(const string &metric, const vector<double> &actuals, const vector<double> &forecasts,
                           const vector<double> &lower_90, const vector<double> &upper_90) {
    if (actuals.empty() || forecasts.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    if (metric == "mae") {
        double sum = 0;
        for (size_t i = 0; i < actuals.size(); i++) {
            sum += std::abs(actuals[i] - forecasts[i]);
        }
        return sum / actuals.size();
    }
    else if (metric == "mse") {
        double sum = 0;
        for (size_t i = 0; i < actuals.size(); i++) {
            double err = actuals[i] - forecasts[i];
            sum += err * err;
        }
        return sum / actuals.size();
    }
    else if (metric == "rmse") {
        double sum = 0;
        for (size_t i = 0; i < actuals.size(); i++) {
            double err = actuals[i] - forecasts[i];
            sum += err * err;
        }
        return std::sqrt(sum / actuals.size());
    }
    else if (metric == "mape") {
        double sum = 0;
        size_t count = 0;
        for (size_t i = 0; i < actuals.size(); i++) {
            if (actuals[i] != 0) {
                sum += std::abs((actuals[i] - forecasts[i]) / actuals[i]);
                count++;
            }
        }
        return count > 0 ? (sum / count) * 100.0 : std::numeric_limits<double>::quiet_NaN();
    }
    else if (metric == "smape") {
        double sum = 0;
        size_t count = 0;
        for (size_t i = 0; i < actuals.size(); i++) {
            double denom = std::abs(actuals[i]) + std::abs(forecasts[i]);
            if (denom > 0) {
                sum += std::abs(actuals[i] - forecasts[i]) / denom;
                count++;
            }
        }
        return count > 0 ? (sum / count) * 200.0 : std::numeric_limits<double>::quiet_NaN();
    }
    else if (metric == "bias") {
        double sum = 0;
        for (size_t i = 0; i < actuals.size(); i++) {
            sum += forecasts[i] - actuals[i];
        }
        return sum / actuals.size();
    }
    else if (metric == "r2") {
        double mean_actual = 0;
        for (auto &a : actuals) mean_actual += a;
        mean_actual /= actuals.size();

        double ss_res = 0, ss_tot = 0;
        for (size_t i = 0; i < actuals.size(); i++) {
            double err = actuals[i] - forecasts[i];
            ss_res += err * err;
            double diff = actuals[i] - mean_actual;
            ss_tot += diff * diff;
        }
        return ss_tot > 0 ? 1.0 - (ss_res / ss_tot) : std::numeric_limits<double>::quiet_NaN();
    }
    else if (metric == "coverage") {
        if (lower_90.size() != actuals.size() || upper_90.size() != actuals.size()) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        size_t covered = 0;
        for (size_t i = 0; i < actuals.size(); i++) {
            if (actuals[i] >= lower_90[i] && actuals[i] <= upper_90[i]) {
                covered++;
            }
        }
        return static_cast<double>(covered) / actuals.size();
    }

    // Default to RMSE
    double sum = 0;
    for (size_t i = 0; i < actuals.size(); i++) {
        double err = actuals[i] - forecasts[i];
        sum += err * err;
    }
    return std::sqrt(sum / actuals.size());
}

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsBacktestNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsBacktestNativeBindData>();

    // Input table must have exactly 3 columns: group, date, value
    if (input.input_table_types.size() != 3) {
        throw InvalidInputException(
            "ts_backtest_native requires input with exactly 3 columns: group_col, date_col, value_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Parse positional arguments: horizon, folds, frequency
    if (input.inputs.size() >= 2) {
        bind_data->horizon = input.inputs[1].GetValue<int64_t>();
    }
    if (input.inputs.size() >= 3) {
        bind_data->folds = input.inputs[2].GetValue<int64_t>();
    }
    if (input.inputs.size() >= 4) {
        string freq_str = input.inputs[3].GetValue<string>();
        auto [freq, is_raw] = ParseFrequencyToSeconds(freq_str);
        bind_data->frequency_seconds = freq;
        bind_data->frequency_is_raw = is_raw;
    }

    // Parse optional params (index 4)
    if (input.inputs.size() >= 5 && !input.inputs[4].IsNull()) {
        auto &params = input.inputs[4];
        bind_data->method = ParseMethodFromParams(params);
        bind_data->model_spec = ParseStringFromParams(params, "model", "");
        bind_data->window_type = ParseStringFromParams(params, "window_type", "expanding");
        bind_data->min_train_size = ParseInt64FromParams(params, "min_train_size", 1);
        bind_data->gap = ParseInt64FromParams(params, "gap", 0);
        bind_data->embargo = ParseInt64FromParams(params, "embargo", 0);
        bind_data->initial_train_size = ParseInt64FromParams(params, "initial_train_size", -1);
        bind_data->skip_length = ParseInt64FromParams(params, "skip_length", -1);
        bind_data->clip_horizon = ParseBoolFromParams(params, "clip_horizon", false);
    }

    // Parse metric (index 5)
    if (input.inputs.size() >= 6 && !input.inputs[5].IsNull()) {
        bind_data->metric = input.inputs[5].GetValue<string>();
    }

    // Detect date column type from input (column 1)
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

    // Output schema: fold_id, group_col, date, forecast, actual, error, abs_error,
    //                lower_90, upper_90, model_name, fold_metric_score
    names.push_back("fold_id");
    return_types.push_back(LogicalType::BIGINT);

    names.push_back(input.input_table_names[0]);  // group_col
    return_types.push_back(bind_data->group_logical_type);

    names.push_back(input.input_table_names[1]);  // date
    return_types.push_back(bind_data->date_logical_type);

    names.push_back("forecast");
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back("actual");
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back("error");
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back("abs_error");
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back("lower_90");
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back("upper_90");
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back("model_name");
    return_types.push_back(LogicalType::VARCHAR);

    names.push_back("fold_metric_score");
    return_types.push_back(LogicalType::DOUBLE);

    return bind_data;
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsBacktestNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<GlobalTableFunctionState>();
}

static unique_ptr<LocalTableFunctionState> TsBacktestNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsBacktestNativeLocalState>();
}

// ============================================================================
// In-Out Function - buffers incoming data
// ============================================================================

static OperatorResultType TsBacktestNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsBacktestNativeBindData>();
    auto &local_state = data_p.local_state->Cast<TsBacktestNativeLocalState>();

    // Buffer all incoming data - we need complete groups and full date range
    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull() || value_val.IsNull()) continue;

        string group_key = GetGroupKey(group_val);

        if (local_state.groups.find(group_key) == local_state.groups.end()) {
            local_state.groups[group_key] = TsBacktestNativeLocalState::GroupData();
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
        grp.values.push_back(value_val.GetValue<double>());
    }

    // Don't output anything during input phase
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize - compute folds and stream output
// ============================================================================

static void ComputeFoldBoundaries(
    TsBacktestNativeLocalState &local_state,
    const TsBacktestNativeBindData &bind_data) {

    if (local_state.groups.empty()) return;

    // Find global date range
    int64_t min_date = std::numeric_limits<int64_t>::max();
    int64_t max_date = std::numeric_limits<int64_t>::min();
    std::set<int64_t> unique_dates;

    for (auto &[key, grp] : local_state.groups) {
        for (auto dt : grp.dates) {
            min_date = std::min(min_date, dt);
            max_date = std::max(max_date, dt);
            unique_dates.insert(dt);
        }
    }

    int64_t n_dates = unique_dates.size();
    if (n_dates < 2) return;

    // Compute frequency in microseconds
    int64_t freq_micros;
    if (bind_data.date_col_type == DateColumnType::INTEGER ||
        bind_data.date_col_type == DateColumnType::BIGINT) {
        freq_micros = bind_data.frequency_is_raw ? bind_data.frequency_seconds : bind_data.frequency_seconds;
    } else {
        freq_micros = bind_data.frequency_is_raw
            ? bind_data.frequency_seconds * 86400LL * 1000000LL
            : bind_data.frequency_seconds * 1000000LL;
    }

    // Compute initial train size
    int64_t init_train_size = bind_data.initial_train_size > 0
        ? bind_data.initial_train_size
        : std::max(static_cast<int64_t>(n_dates / 2), static_cast<int64_t>(1));

    // Compute skip length
    int64_t skip_length = bind_data.skip_length > 0
        ? bind_data.skip_length
        : bind_data.horizon;

    // Generate fold boundaries
    for (int64_t fold = 0; fold < bind_data.folds; fold++) {
        int64_t train_end = min_date + (init_train_size + fold * skip_length) * freq_micros;
        int64_t test_start = train_end + (bind_data.gap + 1) * freq_micros;
        int64_t test_end = train_end + (bind_data.gap + bind_data.horizon) * freq_micros;

        // Clip test_end if clip_horizon is true
        if (bind_data.clip_horizon) {
            test_end = std::min(test_end, max_date);
        }

        // Check if fold is valid (has test data)
        bool valid = bind_data.clip_horizon
            ? (test_start <= max_date)
            : (test_end <= max_date);

        if (!valid) break;

        // Compute train_start based on window type
        int64_t train_start;
        if (bind_data.window_type == "expanding") {
            train_start = min_date;
        } else {  // fixed or sliding
            train_start = train_end - bind_data.min_train_size * freq_micros;
        }

        // Apply embargo from previous fold
        if (fold > 0 && bind_data.embargo > 0 && !local_state.fold_bounds.empty()) {
            int64_t embargo_cutoff = local_state.fold_bounds.back().test_end + bind_data.embargo * freq_micros;
            train_start = std::max(train_start, embargo_cutoff);
        }

        TsBacktestNativeLocalState::FoldBoundary fb;
        fb.fold_id = fold + 1;
        fb.train_start = train_start;
        fb.train_end = train_end;
        fb.test_start = test_start;
        fb.test_end = test_end;

        local_state.fold_bounds.push_back(fb);
    }
}

static void ProcessFold(
    TsBacktestNativeLocalState &local_state,
    const TsBacktestNativeBindData &bind_data,
    idx_t fold_idx) {

    if (fold_idx >= local_state.fold_bounds.size()) return;

    auto &fold = local_state.fold_bounds[fold_idx];

    // Store per-group results for metric computation
    struct GroupFoldResults {
        vector<double> actuals;
        vector<double> forecasts;
        vector<double> lower_90;
        vector<double> upper_90;
        vector<TsBacktestNativeLocalState::BacktestResult> results;
    };
    std::map<string, GroupFoldResults> group_results;

    // Process each group for this fold
    for (const auto &group_key : local_state.group_order) {
        auto &grp = local_state.groups[group_key];

        // Extract training data for this fold
        vector<std::pair<int64_t, double>> train_data;
        std::map<int64_t, double> test_data;

        for (size_t i = 0; i < grp.dates.size(); i++) {
            int64_t dt = grp.dates[i];
            if (dt >= fold.train_start && dt <= fold.train_end) {
                train_data.push_back({dt, grp.values[i]});
            }
            if (dt >= fold.test_start && dt <= fold.test_end) {
                test_data[dt] = grp.values[i];
            }
        }

        // Sort training data by date
        std::sort(train_data.begin(), train_data.end());

        if (train_data.empty() || test_data.empty()) continue;

        // Extract values for FFI
        vector<double> train_values;
        int64_t last_train_date = train_data.back().first;
        for (auto &[dt, val] : train_data) {
            train_values.push_back(val);
        }

        // Compute frequency in microseconds for date arithmetic
        int64_t freq_micros;
        if (bind_data.date_col_type == DateColumnType::INTEGER ||
            bind_data.date_col_type == DateColumnType::BIGINT) {
            freq_micros = bind_data.frequency_is_raw ? bind_data.frequency_seconds : bind_data.frequency_seconds;
        } else {
            freq_micros = bind_data.frequency_is_raw
                ? bind_data.frequency_seconds * 86400LL * 1000000LL
                : bind_data.frequency_seconds * 1000000LL;
        }

        // Call FFI forecast
        ForecastOptions opts = {};
        opts.horizon = static_cast<int32_t>(bind_data.horizon);

        // Build method string and copy to opts.model char array
        string full_method = bind_data.method;
        if (!bind_data.model_spec.empty()) {
            full_method += ":" + bind_data.model_spec;
        }
        std::strncpy(opts.model, full_method.c_str(), sizeof(opts.model) - 1);
        opts.model[sizeof(opts.model) - 1] = '\0';

        ForecastResult fcst = {};
        AnofoxError error = {};

        bool success = anofox_ts_forecast(
            train_values.data(),
            nullptr,  // No validity mask
            train_values.size(),
            &opts,
            &fcst,
            &error
        );

        if (!success) {
            // Skip this group on error
            continue;
        }

        // Generate forecast dates and match with test data
        auto &gfr = group_results[group_key];

        for (size_t h = 0; h < fcst.n_forecasts; h++) {
            int64_t forecast_date = last_train_date + (h + 1) * freq_micros;

            // Find matching test actual
            auto it = test_data.find(forecast_date);
            if (it == test_data.end()) continue;

            double actual = it->second;
            double forecast_val = fcst.point_forecasts[h];
            double lower = fcst.lower_bounds ? fcst.lower_bounds[h] : 0.0;
            double upper = fcst.upper_bounds ? fcst.upper_bounds[h] : 0.0;

            TsBacktestNativeLocalState::BacktestResult res;
            res.fold_id = fold.fold_id;
            res.group_key = group_key;
            res.group_value = grp.group_value;
            res.date = forecast_date;
            res.forecast = forecast_val;
            res.actual = actual;
            res.error = forecast_val - actual;
            res.abs_error = std::abs(res.error);
            res.lower_90 = lower;
            res.upper_90 = upper;
            res.model_name = fcst.model_name[0] != '\0' ? string(fcst.model_name) : bind_data.method;
            res.fold_metric_score = 0.0;  // Will be filled later

            gfr.actuals.push_back(actual);
            gfr.forecasts.push_back(forecast_val);
            gfr.lower_90.push_back(lower);
            gfr.upper_90.push_back(upper);
            gfr.results.push_back(res);
        }

        anofox_free_forecast_result(&fcst);
    }

    // Compute fold metric across all groups
    vector<double> all_actuals, all_forecasts, all_lower, all_upper;
    for (auto &[key, gfr] : group_results) {
        all_actuals.insert(all_actuals.end(), gfr.actuals.begin(), gfr.actuals.end());
        all_forecasts.insert(all_forecasts.end(), gfr.forecasts.begin(), gfr.forecasts.end());
        all_lower.insert(all_lower.end(), gfr.lower_90.begin(), gfr.lower_90.end());
        all_upper.insert(all_upper.end(), gfr.upper_90.begin(), gfr.upper_90.end());
    }

    double fold_metric = ComputeMetric(bind_data.metric, all_actuals, all_forecasts, all_lower, all_upper);

    // Add results with fold metric score
    for (auto &[key, gfr] : group_results) {
        for (auto &res : gfr.results) {
            res.fold_metric_score = fold_metric;
            local_state.results.push_back(res);
        }
    }
}

static OperatorFinalizeResultType TsBacktestNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsBacktestNativeBindData>();
    auto &local_state = data_p.local_state->Cast<TsBacktestNativeLocalState>();

    // Compute fold boundaries on first call
    if (!local_state.folds_computed) {
        ComputeFoldBoundaries(local_state, bind_data);
        local_state.folds_computed = true;
    }

    // Process folds one at a time (memory efficient)
    while (local_state.current_fold < local_state.fold_bounds.size() &&
           local_state.results.size() < STANDARD_VECTOR_SIZE * 2) {
        ProcessFold(local_state, bind_data, local_state.current_fold);
        local_state.current_fold++;
    }

    // Output results
    if (local_state.output_offset >= local_state.results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t output_count = 0;

    while (output_count < STANDARD_VECTOR_SIZE &&
           local_state.output_offset < local_state.results.size()) {

        auto &res = local_state.results[local_state.output_offset];
        idx_t out_idx = output_count;

        // fold_id
        output.data[0].SetValue(out_idx, Value::BIGINT(res.fold_id));

        // group_col
        output.data[1].SetValue(out_idx, res.group_value);

        // date (with type preservation)
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                output.data[2].SetValue(out_idx, Value::DATE(MicrosecondsToDate(res.date)));
                break;
            case DateColumnType::TIMESTAMP:
                output.data[2].SetValue(out_idx, Value::TIMESTAMP(MicrosecondsToTimestamp(res.date)));
                break;
            case DateColumnType::INTEGER:
                output.data[2].SetValue(out_idx, Value::INTEGER(static_cast<int32_t>(res.date)));
                break;
            case DateColumnType::BIGINT:
                output.data[2].SetValue(out_idx, Value::BIGINT(res.date));
                break;
        }

        // forecast
        output.data[3].SetValue(out_idx, Value::DOUBLE(res.forecast));

        // actual
        output.data[4].SetValue(out_idx, Value::DOUBLE(res.actual));

        // error
        output.data[5].SetValue(out_idx, Value::DOUBLE(res.error));

        // abs_error
        output.data[6].SetValue(out_idx, Value::DOUBLE(res.abs_error));

        // lower_90
        output.data[7].SetValue(out_idx, Value::DOUBLE(res.lower_90));

        // upper_90
        output.data[8].SetValue(out_idx, Value::DOUBLE(res.upper_90));

        // model_name
        output.data[9].SetValue(out_idx, Value::CreateValue(res.model_name));

        // fold_metric_score
        output.data[10].SetValue(out_idx, Value::DOUBLE(res.fold_metric_score));

        output_count++;
        local_state.output_offset++;
    }

    output.SetCardinality(output_count);

    // Check if more folds to process or more results to output
    if (local_state.current_fold < local_state.fold_bounds.size() ||
        local_state.output_offset < local_state.results.size()) {
        return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
    }

    return OperatorFinalizeResultType::FINISHED;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsBacktestNativeFunction(ExtensionLoader &loader) {
    // Table-in-out function: (TABLE, horizon, folds, frequency, params, metric)
    // Input table must have 3 columns: group_col, date_col, value_col
    TableFunction func("ts_backtest_native",
        {LogicalType::TABLE,
         LogicalType::BIGINT,   // horizon
         LogicalType::BIGINT,   // folds
         LogicalType::VARCHAR,  // frequency
         LogicalType::ANY,      // params (MAP or STRUCT)
         LogicalType::VARCHAR}, // metric
        nullptr,  // No execute function - use in_out_function
        TsBacktestNativeBind,
        TsBacktestNativeInitGlobal,
        TsBacktestNativeInitLocal);

    func.in_out_function = TsBacktestNativeInOut;
    func.in_out_function_final = TsBacktestNativeFinalize;

    loader.RegisterFunction(func);

    // Also register with anofox_fcst prefix
    TableFunction anofox_func = func;
    anofox_func.name = "anofox_fcst_ts_backtest_native";
    loader.RegisterFunction(anofox_func);
}

} // namespace duckdb
