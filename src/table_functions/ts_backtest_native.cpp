#include "ts_backtest_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For DateColumnType, helper functions
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <cmath>
#include <cstring>
#include <set>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace duckdb {

// ============================================================================
// _ts_backtest_native - Internal native streaming backtest table function
//
// This is an INTERNAL function used by ts_backtest_auto_by macro.
// Users should call ts_backtest_auto_by() instead of this function directly.
//
// PARALLEL EXECUTION STRATEGY:
// This function uses a two-phase approach to enable parallelization while
// maintaining correctness for fold boundary computation:
//
// Phase 1 (in_out_function):
//   - Each thread buffers its partition of data in LocalState
//   - Atomically updates global min/max dates in GlobalState
//
// Phase 2 (finalize):
//   - First thread to enter computes fold boundaries from global date range
//   - All threads wait for fold boundaries to be ready
//   - Each thread then processes its own groups in parallel
//
// MEMORY FOOTPRINT (1M rows = 10k series x 100 dates):
//   - Native (this function): ~31 MB peak buffer memory
//   - Old SQL macro approach: ~1.9 GB peak buffer memory (62x more!)
//
// PERFORMANCE (1M rows, 10k series):
//   - Native parallel: 0.26s latency
//   - Native single-thread: 0.94s latency (3.6x slower)
//   - Old SQL macro: 0.54s latency (2x slower)
//
// ============================================================================

// ============================================================================
// Bind Data - captures all parameters
// ============================================================================

struct TsBacktestNativeBindData : public TableFunctionData {
    // Required parameters
    int64_t horizon = 7;
    int64_t folds = 5;

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
// Fold Boundary Structure (position-based indices)
//
// IMPORTANT: This uses position-based indices, not timestamps.
// The assumption is that input data is pre-cleaned (no gaps, proper frequency).
// This design eliminates calendar frequency issues (monthly, quarterly, yearly)
// where date arithmetic doesn't align with actual data timestamps.
// ============================================================================

struct FoldBoundary {
    int64_t fold_id;
    idx_t train_start_idx;   // Start index in sorted data (inclusive)
    idx_t train_end_idx;     // End index in sorted data (inclusive)
    idx_t test_start_idx;    // Start index for test set (inclusive)
    idx_t test_end_idx;      // End index for test set (inclusive)
};

// ============================================================================
// Global State - manages synchronization and shared fold boundaries
// ============================================================================

struct TsBacktestNativeGlobalState : public GlobalTableFunctionState {
    // Allow parallel execution - each thread processes its groups
    idx_t MaxThreads() const override {
        return 999999;  // Unlimited - let DuckDB decide
    }

    // Atomic date range tracking (updated by all threads)
    std::atomic<int64_t> global_min_date{std::numeric_limits<int64_t>::max()};
    std::atomic<int64_t> global_max_date{std::numeric_limits<int64_t>::min()};

    // Unique dates collection (protected by mutex)
    std::mutex dates_mutex;
    std::set<int64_t> unique_dates;

    // Fold boundaries (computed once, read by all threads)
    std::mutex fold_mutex;
    std::condition_variable fold_cv;
    std::atomic<bool> fold_bounds_computed{false};
    vector<FoldBoundary> fold_bounds;

    // Thread synchronization for finalize phase
    std::atomic<int> threads_contributing{0};
    std::atomic<int> threads_waiting{0};
    std::atomic<bool> all_dates_collected{false};
};

// ============================================================================
// Local State - buffers data per thread and manages streaming output
// ============================================================================

struct TsBacktestNativeLocalState : public LocalTableFunctionState {
    // Input data buffer per group (this thread's partition)
    struct GroupData {
        Value group_value;
        vector<int64_t> dates;  // microseconds
        vector<double> values;
    };

    std::map<string, GroupData> groups;
    vector<string> group_order;

    // Local date stats (merged into global at end of in_out phase)
    int64_t local_min_date = std::numeric_limits<int64_t>::max();
    int64_t local_max_date = std::numeric_limits<int64_t>::min();
    std::set<int64_t> local_unique_dates;

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
    bool dates_contributed = false;
    bool processing_started = false;
    idx_t current_fold = 0;
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
            "_ts_backtest_native requires input with exactly 3 columns: group_col, date_col, value_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Parse positional arguments: horizon, folds
    if (input.inputs.size() >= 2) {
        bind_data->horizon = input.inputs[1].GetValue<int64_t>();
    }
    if (input.inputs.size() >= 3) {
        bind_data->folds = input.inputs[2].GetValue<int64_t>();
    }

    // Parse optional params (index 3)
    if (input.inputs.size() >= 4 && !input.inputs[3].IsNull()) {
        auto &params = input.inputs[3];
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

    // Parse metric (index 4)
    if (input.inputs.size() >= 5 && !input.inputs[4].IsNull()) {
        bind_data->metric = input.inputs[4].GetValue<string>();
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
    return make_uniq<TsBacktestNativeGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsBacktestNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsBacktestNativeLocalState>();
}

// ============================================================================
// In-Out Function - buffers incoming data and tracks date range
// ============================================================================

static OperatorResultType TsBacktestNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsBacktestNativeBindData>();
    auto &local_state = data_p.local_state->Cast<TsBacktestNativeLocalState>();

    // Buffer all incoming data - we need complete groups
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

        // Convert date to microseconds (truncate to seconds for timestamp consistency)
        int64_t date_micros;
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                date_micros = DateToMicroseconds(date_val.GetValue<date_t>());
                break;
            case DateColumnType::TIMESTAMP: {
                date_micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
                // Truncate to seconds (remove sub-second precision)
                constexpr int64_t MICROS_PER_SECOND = 1000000;
                date_micros = (date_micros / MICROS_PER_SECOND) * MICROS_PER_SECOND;
                break;
            }
            case DateColumnType::INTEGER:
                date_micros = date_val.GetValue<int32_t>();
                break;
            case DateColumnType::BIGINT:
                date_micros = date_val.GetValue<int64_t>();
                break;
        }

        grp.dates.push_back(date_micros);
        grp.values.push_back(value_val.GetValue<double>());

        // Track local date stats
        local_state.local_min_date = std::min(local_state.local_min_date, date_micros);
        local_state.local_max_date = std::max(local_state.local_max_date, date_micros);
        local_state.local_unique_dates.insert(date_micros);
    }

    // Don't output anything during input phase
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Compute Fold Boundaries (position-based, called once from first thread)
//
// POSITION-BASED FOLD COMPUTATION:
// This function computes fold boundaries as array indices, not timestamps.
// This handles all frequency types (hourly, daily, weekly, monthly, quarterly,
// yearly) correctly because it relies on data positions, not date arithmetic.
//
// ASSUMPTION: Input data is pre-cleaned with no gaps and consistent frequency.
// Any frequency issues should be resolved before calling backtest functions.
//
// Walk-forward CV approach:
//   Fold 1: train [0..init_train-1], test [init_train+gap..init_train+gap+horizon-1]
//   Fold 2: train [0..init_train+skip-1], test [init_train+skip+gap..]
//   etc.
// ============================================================================

static void ComputeFoldBoundaries(
    TsBacktestNativeGlobalState &global_state,
    const TsBacktestNativeBindData &bind_data) {

    idx_t n_dates = global_state.unique_dates.size();

    if (n_dates < 2) return;

    // Compute initial train size (number of data points, not time periods)
    // Default: position folds so the last fold's test ends at data end
    // Formula: init = n_dates - horizon * folds (with skip_length = horizon)
    // This ensures test set covers the most recent data
    idx_t init_train_size;
    if (bind_data.initial_train_size > 0) {
        init_train_size = static_cast<idx_t>(bind_data.initial_train_size);
    } else {
        idx_t folds = static_cast<idx_t>(bind_data.folds);
        idx_t horizon = static_cast<idx_t>(bind_data.horizon);
        // For folds=1, horizon=12, n_dates=36: init = 36 - 12 = 24
        // Test will be indices 24-35 (last 12 points)
        idx_t needed = horizon * folds;
        init_train_size = (n_dates > needed) ? (n_dates - needed) : 1;
    }

    // Compute skip length (number of data points to advance between folds)
    idx_t skip_length = bind_data.skip_length > 0
        ? static_cast<idx_t>(bind_data.skip_length)
        : static_cast<idx_t>(bind_data.horizon);

    idx_t gap = static_cast<idx_t>(bind_data.gap);
    idx_t horizon = static_cast<idx_t>(bind_data.horizon);
    idx_t embargo = static_cast<idx_t>(bind_data.embargo);
    idx_t min_train = static_cast<idx_t>(bind_data.min_train_size);

    // Generate fold boundaries using position indices
    for (int64_t fold = 0; fold < bind_data.folds; fold++) {
        // Training end index (inclusive) - advances by skip_length each fold
        idx_t train_end_idx = init_train_size - 1 + fold * skip_length;

        // Test start index (inclusive) - starts after gap
        idx_t test_start_idx = train_end_idx + 1 + gap;

        // Test end index (inclusive)
        idx_t test_end_idx = test_start_idx + horizon - 1;

        // Clip test_end if clip_horizon is true
        if (bind_data.clip_horizon && test_end_idx >= n_dates) {
            test_end_idx = n_dates - 1;
        }

        // Check if fold is valid (has test data within data range)
        bool valid = bind_data.clip_horizon
            ? (test_start_idx < n_dates)
            : (test_end_idx < n_dates);

        if (!valid) break;

        // Compute train_start based on window type
        idx_t train_start_idx;
        if (bind_data.window_type == "expanding") {
            train_start_idx = 0;
        } else {
            // fixed or sliding window - use min_train_size points
            if (train_end_idx + 1 >= min_train) {
                train_start_idx = train_end_idx + 1 - min_train;
            } else {
                train_start_idx = 0;
            }
        }

        // Apply embargo from previous fold
        if (fold > 0 && embargo > 0 && !global_state.fold_bounds.empty()) {
            idx_t embargo_cutoff = global_state.fold_bounds.back().test_end_idx + 1 + embargo;
            if (embargo_cutoff > train_start_idx) {
                train_start_idx = embargo_cutoff;
            }
        }

        FoldBoundary fb;
        fb.fold_id = fold + 1;
        fb.train_start_idx = train_start_idx;
        fb.train_end_idx = train_end_idx;
        fb.test_start_idx = test_start_idx;
        fb.test_end_idx = test_end_idx;

        global_state.fold_bounds.push_back(fb);
    }
}

// ============================================================================
// Process Fold for a single group (position-based)
//
// POSITION-BASED APPROACH:
// 1. Sort group data by date once
// 2. Slice train data by indices [train_start_idx..train_end_idx]
// 3. Slice test data by indices [test_start_idx..test_end_idx]
// 4. Match forecast[h] to test[h] directly by position
//
// No date arithmetic needed - dates come directly from the data at each index.
// This correctly handles all frequency types including calendar-based ones.
// ============================================================================

static void ProcessGroupFold(
    TsBacktestNativeLocalState &local_state,
    const TsBacktestNativeBindData &bind_data,
    const FoldBoundary &fold,
    const string &group_key,
    TsBacktestNativeLocalState::GroupData &grp,
    vector<double> &fold_actuals,
    vector<double> &fold_forecasts,
    vector<double> &fold_lower,
    vector<double> &fold_upper) {

    // Sort group data by date (creates sorted indices)
    vector<size_t> sorted_indices(grp.dates.size());
    for (size_t i = 0; i < sorted_indices.size(); i++) {
        sorted_indices[i] = i;
    }
    std::sort(sorted_indices.begin(), sorted_indices.end(),
        [&grp](size_t a, size_t b) { return grp.dates[a] < grp.dates[b]; });

    idx_t n_points = sorted_indices.size();

    // Validate fold indices against this group's data size
    if (fold.train_end_idx >= n_points || fold.test_start_idx >= n_points) {
        return;  // Not enough data for this fold
    }

    // Clip test_end_idx to available data
    idx_t effective_test_end = std::min(fold.test_end_idx, n_points - 1);

    // Extract training data by position
    vector<double> train_values;
    for (idx_t i = fold.train_start_idx; i <= fold.train_end_idx && i < n_points; i++) {
        train_values.push_back(grp.values[sorted_indices[i]]);
    }

    if (train_values.empty()) return;

    // Prepare test data (dates and values at test positions)
    vector<std::pair<int64_t, double>> test_points;
    for (idx_t i = fold.test_start_idx; i <= effective_test_end && i < n_points; i++) {
        size_t orig_idx = sorted_indices[i];
        test_points.push_back({grp.dates[orig_idx], grp.values[orig_idx]});
    }

    if (test_points.empty()) return;

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
        return;
    }

    // Match forecasts with test data by position (forecast[h] -> test[h])
    // No date arithmetic - we use the actual dates from test_points
    size_t n_matches = std::min(static_cast<size_t>(fcst.n_forecasts), test_points.size());
    for (size_t h = 0; h < n_matches; h++) {
        int64_t actual_date = test_points[h].first;
        double actual = test_points[h].second;
        double forecast_val = fcst.point_forecasts[h];
        double lower = fcst.lower_bounds ? fcst.lower_bounds[h] : 0.0;
        double upper = fcst.upper_bounds ? fcst.upper_bounds[h] : 0.0;

        TsBacktestNativeLocalState::BacktestResult res;
        res.fold_id = fold.fold_id;
        res.group_key = group_key;
        res.group_value = grp.group_value;
        res.date = actual_date;  // Use actual date from data, not calculated
        res.forecast = forecast_val;
        res.actual = actual;
        res.error = forecast_val - actual;
        res.abs_error = std::abs(res.error);
        res.lower_90 = lower;
        res.upper_90 = upper;
        res.model_name = fcst.model_name[0] != '\0' ? string(fcst.model_name) : bind_data.method;
        res.fold_metric_score = 0.0;  // Will be filled later

        fold_actuals.push_back(actual);
        fold_forecasts.push_back(forecast_val);
        fold_lower.push_back(lower);
        fold_upper.push_back(upper);
        local_state.results.push_back(res);
    }

    anofox_free_forecast_result(&fcst);
}

// ============================================================================
// Finalize - synchronize threads, compute folds, and stream output
// ============================================================================

static OperatorFinalizeResultType TsBacktestNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsBacktestNativeBindData>();
    auto &global_state = data_p.global_state->Cast<TsBacktestNativeGlobalState>();
    auto &local_state = data_p.local_state->Cast<TsBacktestNativeLocalState>();

    // Phase 1: Contribute local date stats to global state (once per thread)
    if (!local_state.dates_contributed) {
        // Atomically update global min/max dates
        int64_t current_min = global_state.global_min_date.load();
        while (local_state.local_min_date < current_min &&
               !global_state.global_min_date.compare_exchange_weak(current_min, local_state.local_min_date)) {
            // Retry if CAS failed
        }

        int64_t current_max = global_state.global_max_date.load();
        while (local_state.local_max_date > current_max &&
               !global_state.global_max_date.compare_exchange_weak(current_max, local_state.local_max_date)) {
            // Retry if CAS failed
        }

        // Contribute unique dates under lock
        {
            std::lock_guard<std::mutex> lock(global_state.dates_mutex);
            global_state.unique_dates.insert(
                local_state.local_unique_dates.begin(),
                local_state.local_unique_dates.end());
        }

        local_state.dates_contributed = true;

        // Signal that this thread has contributed
        global_state.threads_contributing.fetch_add(1);
    }

    // Phase 2: Wait for fold boundaries to be computed
    if (!global_state.fold_bounds_computed.load()) {
        std::unique_lock<std::mutex> lock(global_state.fold_mutex);

        // Double-check after acquiring lock
        if (!global_state.fold_bounds_computed.load()) {
            // First thread to get here computes fold boundaries
            ComputeFoldBoundaries(global_state, bind_data);
            global_state.fold_bounds_computed.store(true);
            global_state.fold_cv.notify_all();
        }
    }

    // Phase 3: Process this thread's groups (parallel across threads)
    if (!local_state.processing_started) {
        local_state.processing_started = true;

        // Process all folds for this thread's groups
        for (const auto &fold : global_state.fold_bounds) {
            vector<double> fold_actuals, fold_forecasts, fold_lower, fold_upper;
            size_t results_start = local_state.results.size();

            // Process each group this thread owns
            for (const auto &group_key : local_state.group_order) {
                auto &grp = local_state.groups[group_key];
                ProcessGroupFold(local_state, bind_data, fold, group_key, grp,
                               fold_actuals, fold_forecasts, fold_lower, fold_upper);
            }

            // Compute fold metric for this thread's groups
            double fold_metric = ComputeMetric(bind_data.metric, fold_actuals, fold_forecasts, fold_lower, fold_upper);

            // Update fold_metric_score for all results from this fold
            for (size_t i = results_start; i < local_state.results.size(); i++) {
                local_state.results[i].fold_metric_score = fold_metric;
            }
        }
    }

    // Phase 4: Stream output
    if (local_state.results.empty() || local_state.output_offset >= local_state.results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t output_count = 0;

    // Initialize all output vectors as FLAT_VECTOR for parallel-safe batch merging
    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

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

    if (local_state.output_offset < local_state.results.size()) {
        return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
    }

    return OperatorFinalizeResultType::FINISHED;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsBacktestNativeFunction(ExtensionLoader &loader) {
    // Table-in-out function: (TABLE, horizon, folds, params, metric)
    // Input table must have 3 columns: group_col, date_col, value_col
    //
    // This is an internal function (underscore prefix) used by ts_backtest_auto_by macro.
    // Direct use is discouraged - use ts_backtest_auto_by instead.
    //
    // Uses position-based indexing (not date arithmetic) - frequency parameter removed.
    // Input data must be pre-cleaned, sorted by date, with no gaps.
    TableFunction func("_ts_backtest_native",
        {LogicalType::TABLE,
         LogicalType::BIGINT,   // horizon
         LogicalType::BIGINT,   // folds
         LogicalType::ANY,      // params (MAP or STRUCT)
         LogicalType::VARCHAR}, // metric
        nullptr,  // No execute function - use in_out_function
        TsBacktestNativeBind,
        TsBacktestNativeInitGlobal,
        TsBacktestNativeInitLocal);

    func.in_out_function = TsBacktestNativeInOut;
    func.in_out_function_final = TsBacktestNativeFinalize;

    loader.RegisterFunction(func);

    // Also register with anofox_fcst prefix (internal)
    TableFunction anofox_func = func;
    anofox_func.name = "_anofox_fcst_ts_backtest_native";
    loader.RegisterFunction(anofox_func);
}

} // namespace duckdb
