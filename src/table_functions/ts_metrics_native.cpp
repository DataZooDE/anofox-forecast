#include "ts_metrics_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For DateColumnType enum and date helpers
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <mutex>
#include <cmath>
#include <atomic>
#include <thread>

namespace duckdb {

// ============================================================================
// _ts_metrics_native - Native table function for computing metrics with
// dynamic column exclusion
//
// This function takes:
// - Input table with ALL columns
// - Column names (as VARCHAR) for date, actual, forecast
// - Metric type (mae, mse, rmse, mape, smape, r2, bias)
//
// Output: All columns EXCEPT date/actual/forecast, plus the computed metric
// Grouping: By all remaining columns (GROUP BY ALL equivalent)
// ============================================================================

// Supported metric types
enum class MetricType {
    MAE,
    MSE,
    RMSE,
    MAPE,
    SMAPE,
    R2,
    BIAS
};

static MetricType ParseMetricType(const string &metric_str) {
    string lower = StringUtil::Lower(metric_str);
    if (lower == "mae") return MetricType::MAE;
    if (lower == "mse") return MetricType::MSE;
    if (lower == "rmse") return MetricType::RMSE;
    if (lower == "mape") return MetricType::MAPE;
    if (lower == "smape") return MetricType::SMAPE;
    if (lower == "r2") return MetricType::R2;
    if (lower == "bias") return MetricType::BIAS;
    throw InvalidInputException("Unknown metric type: %s. Supported: mae, mse, rmse, mape, smape, r2, bias", metric_str);
}

static string MetricColumnName(MetricType type) {
    switch (type) {
        case MetricType::MAE: return "mae";
        case MetricType::MSE: return "mse";
        case MetricType::RMSE: return "rmse";
        case MetricType::MAPE: return "mape";
        case MetricType::SMAPE: return "smape";
        case MetricType::R2: return "r2";
        case MetricType::BIAS: return "bias";
    }
    return "metric";
}

// ============================================================================
// Bind Data
// ============================================================================

struct TsMetricsNativeBindData : public TableFunctionData {
    MetricType metric_type = MetricType::RMSE;

    // Column indices in input table
    idx_t date_col_idx = 0;
    idx_t actual_col_idx = 0;
    idx_t forecast_col_idx = 0;

    // Columns to include in output (indices into input)
    vector<idx_t> group_col_indices;

    // Output column info (names and types from input, preserved)
    vector<string> output_col_names;
    vector<LogicalType> output_col_types;

    // Date column type for ordering
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
};

// ============================================================================
// Standalone structs for TsMetricsNative
// ============================================================================

struct MetricsGroupData {
    vector<Value> group_values;
    vector<int64_t> dates;
    vector<double> actuals;
    vector<double> forecasts;
};

struct MetricsOutputRow {
    string group_key;
    vector<Value> group_values;
    double metric_value;
};

// ============================================================================
// Global State
// ============================================================================

struct TsMetricsNativeGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override {
        return 999999;
    }

    std::mutex groups_mutex;
    std::map<string, MetricsGroupData> groups;
    vector<string> group_order;

    vector<MetricsOutputRow> results;
    bool processed = false;
    idx_t output_offset = 0;
    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};
};

// ============================================================================
// Local State
// ============================================================================

struct TsMetricsNativeLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool registered_collector = false;
    bool registered_finalizer = false;
};

// ============================================================================
// Helper: Build group key from multiple column values
// ============================================================================

static string BuildGroupKey(const vector<Value> &values) {
    string key;
    for (idx_t i = 0; i < values.size(); i++) {
        if (i > 0) key += "|";
        key += values[i].ToString();
    }
    return key;
}

// ============================================================================
// Helper: Convert date value to int64 for ordering
// ============================================================================

static int64_t DateValueToInt64(const Value &date_val, DateColumnType date_type) {
    if (date_val.IsNull()) return 0;

    switch (date_type) {
        case DateColumnType::DATE:
            return DateToMicroseconds(date_val.GetValue<date_t>());
        case DateColumnType::TIMESTAMP:
            return TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
        case DateColumnType::INTEGER:
            return date_val.GetValue<int32_t>();
        case DateColumnType::BIGINT:
            return date_val.GetValue<int64_t>();
    }
    return 0;
}

// ============================================================================
// Helper: Compute metric
// ============================================================================

static double ComputeMetric(MetricType type, const vector<double> &actuals,
                           const vector<double> &forecasts) {
    if (actuals.size() != forecasts.size() || actuals.empty()) {
        return std::nan("");
    }

    AnofoxError error;
    double result;
    bool success = false;

    switch (type) {
        case MetricType::MAE:
            success = anofox_ts_mae(actuals.data(), actuals.size(),
                                    forecasts.data(), forecasts.size(),
                                    &result, &error);
            break;
        case MetricType::MSE:
            success = anofox_ts_mse(actuals.data(), actuals.size(),
                                    forecasts.data(), forecasts.size(),
                                    &result, &error);
            break;
        case MetricType::RMSE:
            success = anofox_ts_rmse(actuals.data(), actuals.size(),
                                     forecasts.data(), forecasts.size(),
                                     &result, &error);
            break;
        case MetricType::MAPE:
            success = anofox_ts_mape(actuals.data(), actuals.size(),
                                     forecasts.data(), forecasts.size(),
                                     &result, &error);
            break;
        case MetricType::SMAPE:
            success = anofox_ts_smape(actuals.data(), actuals.size(),
                                      forecasts.data(), forecasts.size(),
                                      &result, &error);
            break;
        case MetricType::R2:
            success = anofox_ts_r2(actuals.data(), actuals.size(),
                                   forecasts.data(), forecasts.size(),
                                   &result, &error);
            break;
        case MetricType::BIAS:
            success = anofox_ts_bias(actuals.data(), actuals.size(),
                                     forecasts.data(), forecasts.size(),
                                     &result, &error);
            break;
    }

    return success ? result : std::nan("");
}

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsMetricsNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsMetricsNativeBindData>();

    // Parse parameters: date_col_name, actual_col_name, forecast_col_name, metric_type
    // TABLE is at index 0, so non-table params start at index 1
    if (input.inputs.size() < 5) {
        throw InvalidInputException(
            "_ts_metrics_native requires: (input_table, date_col_name, actual_col_name, forecast_col_name, metric_type)");
    }

    string date_col_name = input.inputs[1].GetValue<string>();
    string actual_col_name = input.inputs[2].GetValue<string>();
    string forecast_col_name = input.inputs[3].GetValue<string>();
    string metric_type_str = input.inputs[4].GetValue<string>();

    bind_data->metric_type = ParseMetricType(metric_type_str);

    // Find column indices in input table
    auto &col_names = input.input_table_names;
    auto &col_types = input.input_table_types;

    bool found_date = false, found_actual = false, found_forecast = false;

    for (idx_t i = 0; i < col_names.size(); i++) {
        if (col_names[i] == date_col_name) {
            bind_data->date_col_idx = i;
            found_date = true;

            // Determine date type
            auto &dtype = col_types[i];
            if (dtype.id() == LogicalTypeId::DATE) {
                bind_data->date_col_type = DateColumnType::DATE;
            } else if (dtype.id() == LogicalTypeId::TIMESTAMP) {
                bind_data->date_col_type = DateColumnType::TIMESTAMP;
            } else if (dtype.id() == LogicalTypeId::INTEGER) {
                bind_data->date_col_type = DateColumnType::INTEGER;
            } else if (dtype.id() == LogicalTypeId::BIGINT) {
                bind_data->date_col_type = DateColumnType::BIGINT;
            }
        } else if (col_names[i] == actual_col_name) {
            bind_data->actual_col_idx = i;
            found_actual = true;
        } else if (col_names[i] == forecast_col_name) {
            bind_data->forecast_col_idx = i;
            found_forecast = true;
        }
    }

    if (!found_date) {
        throw InvalidInputException("Column '%s' not found in input table", date_col_name);
    }
    if (!found_actual) {
        throw InvalidInputException("Column '%s' not found in input table", actual_col_name);
    }
    if (!found_forecast) {
        throw InvalidInputException("Column '%s' not found in input table", forecast_col_name);
    }

    // Build output schema: all columns EXCEPT date/actual/forecast, plus metric
    for (idx_t i = 0; i < col_names.size(); i++) {
        if (i != bind_data->date_col_idx &&
            i != bind_data->actual_col_idx &&
            i != bind_data->forecast_col_idx) {

            bind_data->group_col_indices.push_back(i);
            bind_data->output_col_names.push_back(col_names[i]);
            bind_data->output_col_types.push_back(col_types[i]);

            names.push_back(col_names[i]);
            return_types.push_back(col_types[i]);
        }
    }

    // Add metric column
    names.push_back(MetricColumnName(bind_data->metric_type));
    return_types.push_back(LogicalType::DOUBLE);

    return bind_data;
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsMetricsNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<TsMetricsNativeGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsMetricsNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsMetricsNativeLocalState>();
}

// ============================================================================
// In-Out Function (Buffer Input)
// ============================================================================

static OperatorResultType TsMetricsNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsMetricsNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsMetricsNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsMetricsNativeLocalState>();

    if (!lstate.registered_collector) {
        gstate.threads_collecting.fetch_add(1);
        lstate.registered_collector = true;
    }

    // Extract batch locally first (no lock)
    struct LocalEntry {
        string group_key;
        vector<Value> group_vals;
        int64_t date_int;
        double actual_dbl;
        double forecast_dbl;
    };
    vector<LocalEntry> batch;
    batch.reserve(input.size());

    for (idx_t i = 0; i < input.size(); i++) {
        vector<Value> group_vals;
        for (idx_t col_idx : bind_data.group_col_indices) {
            group_vals.push_back(input.data[col_idx].GetValue(i));
        }

        string group_key = BuildGroupKey(group_vals);

        Value date_val = input.data[bind_data.date_col_idx].GetValue(i);
        Value actual_val = input.data[bind_data.actual_col_idx].GetValue(i);
        Value forecast_val = input.data[bind_data.forecast_col_idx].GetValue(i);

        if (date_val.IsNull()) continue;

        int64_t date_int = DateValueToInt64(date_val, bind_data.date_col_type);
        double actual_dbl = actual_val.IsNull() ? std::nan("") : actual_val.GetValue<double>();
        double forecast_dbl = forecast_val.IsNull() ? std::nan("") : forecast_val.GetValue<double>();

        batch.push_back({std::move(group_key), std::move(group_vals), date_int, actual_dbl, forecast_dbl});
    }

    // Lock once and insert all
    {
        std::lock_guard<std::mutex> lock(gstate.groups_mutex);
        for (auto &entry : batch) {
            if (gstate.groups.find(entry.group_key) == gstate.groups.end()) {
                gstate.groups[entry.group_key] = MetricsGroupData();
                gstate.groups[entry.group_key].group_values = entry.group_vals;
                gstate.group_order.push_back(entry.group_key);
            }

            auto &grp = gstate.groups[entry.group_key];
            grp.dates.push_back(entry.date_int);
            grp.actuals.push_back(entry.actual_dbl);
            grp.forecasts.push_back(entry.forecast_dbl);
        }
    }

    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function (Process and Output)
// ============================================================================

static OperatorFinalizeResultType TsMetricsNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsMetricsNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsMetricsNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsMetricsNativeLocalState>();

    // Barrier + claim pattern
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
        for (const auto &group_key : gstate.group_order) {
            auto &grp = gstate.groups[group_key];

            if (grp.dates.empty()) continue;

            // Sort by date
            vector<size_t> indices(grp.dates.size());
            for (size_t j = 0; j < indices.size(); j++) indices[j] = j;
            std::sort(indices.begin(), indices.end(),
                [&grp](size_t a, size_t b) { return grp.dates[a] < grp.dates[b]; });

            // Reorder actuals and forecasts
            vector<double> sorted_actuals, sorted_forecasts;
            sorted_actuals.reserve(indices.size());
            sorted_forecasts.reserve(indices.size());

            for (size_t j : indices) {
                if (!std::isnan(grp.actuals[j]) && !std::isnan(grp.forecasts[j])) {
                    sorted_actuals.push_back(grp.actuals[j]);
                    sorted_forecasts.push_back(grp.forecasts[j]);
                }
            }

            // Compute metric
            double metric_val = ComputeMetric(bind_data.metric_type, sorted_actuals, sorted_forecasts);

            // Add to results
            MetricsOutputRow row;
            row.group_key = group_key;
            row.group_values = grp.group_values;
            row.metric_value = metric_val;
            gstate.results.push_back(std::move(row));
        }

        gstate.processed = true;
    }

    // Stream results
    idx_t remaining = gstate.results.size() - gstate.output_offset;
    if (remaining == 0) {
        output.SetCardinality(0);
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t to_output = std::min(remaining, static_cast<idx_t>(STANDARD_VECTOR_SIZE));
    output.SetCardinality(to_output);

    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    for (idx_t i = 0; i < to_output; i++) {
        auto &row = gstate.results[gstate.output_offset + i];

        // Output group columns
        for (idx_t col = 0; col < row.group_values.size(); col++) {
            output.data[col].SetValue(i, row.group_values[col]);
        }

        // Output metric value
        idx_t metric_col = row.group_values.size();
        output.data[metric_col].SetValue(i, Value::DOUBLE(row.metric_value));
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

void RegisterTsMetricsNativeFunction(ExtensionLoader &loader) {
    TableFunction func(
        "_ts_metrics_native",
        {LogicalType::TABLE,    // Input table
         LogicalType::VARCHAR,  // date_col_name
         LogicalType::VARCHAR,  // actual_col_name
         LogicalType::VARCHAR,  // forecast_col_name
         LogicalType::VARCHAR}, // metric_type
        nullptr,
        TsMetricsNativeBind,
        TsMetricsNativeInitGlobal,
        TsMetricsNativeInitLocal);

    func.in_out_function = TsMetricsNativeInOut;
    func.in_out_function_final = TsMetricsNativeFinalize;

    loader.RegisterFunction(func);
}

// ============================================================================
// ============================================================================
// _ts_mase_native - MASE metric with GROUP BY ALL
// ============================================================================
// ============================================================================

struct TsMaseNativeBindData : public TableFunctionData {
    idx_t date_col_idx = 0;
    idx_t actual_col_idx = 0;
    idx_t forecast_col_idx = 0;
    idx_t baseline_col_idx = 0;

    vector<idx_t> group_col_indices;
    vector<string> output_col_names;
    vector<LogicalType> output_col_types;
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
};

// ============================================================================
// Standalone structs for TsMaseNative
// ============================================================================

struct MaseGroupData {
    vector<Value> group_values;
    vector<int64_t> dates;
    vector<double> actuals;
    vector<double> forecasts;
    vector<double> baselines;
};

struct MaseOutputRow {
    string group_key;
    vector<Value> group_values;
    double metric_value;
};

struct TsMaseNativeGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override { return 999999; }

    std::mutex groups_mutex;
    std::map<string, MaseGroupData> groups;
    vector<string> group_order;

    vector<MaseOutputRow> results;
    bool processed = false;
    idx_t output_offset = 0;
    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};
};

struct TsMaseNativeLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool registered_collector = false;
    bool registered_finalizer = false;
};

static unique_ptr<FunctionData> TsMaseNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsMaseNativeBindData>();

    if (input.inputs.size() < 5) {
        throw InvalidInputException(
            "_ts_mase_native requires: (input_table, date_col, actual_col, forecast_col, baseline_col)");
    }

    string date_col_name = input.inputs[1].GetValue<string>();
    string actual_col_name = input.inputs[2].GetValue<string>();
    string forecast_col_name = input.inputs[3].GetValue<string>();
    string baseline_col_name = input.inputs[4].GetValue<string>();

    auto &col_names = input.input_table_names;
    auto &col_types = input.input_table_types;

    bool found_date = false, found_actual = false, found_forecast = false, found_baseline = false;

    for (idx_t i = 0; i < col_names.size(); i++) {
        if (col_names[i] == date_col_name) {
            bind_data->date_col_idx = i;
            found_date = true;
            auto &dtype = col_types[i];
            if (dtype.id() == LogicalTypeId::DATE) {
                bind_data->date_col_type = DateColumnType::DATE;
            } else if (dtype.id() == LogicalTypeId::TIMESTAMP) {
                bind_data->date_col_type = DateColumnType::TIMESTAMP;
            } else if (dtype.id() == LogicalTypeId::INTEGER) {
                bind_data->date_col_type = DateColumnType::INTEGER;
            } else if (dtype.id() == LogicalTypeId::BIGINT) {
                bind_data->date_col_type = DateColumnType::BIGINT;
            }
        } else if (col_names[i] == actual_col_name) {
            bind_data->actual_col_idx = i;
            found_actual = true;
        } else if (col_names[i] == forecast_col_name) {
            bind_data->forecast_col_idx = i;
            found_forecast = true;
        } else if (col_names[i] == baseline_col_name) {
            bind_data->baseline_col_idx = i;
            found_baseline = true;
        }
    }

    if (!found_date) throw InvalidInputException("Column '%s' not found", date_col_name);
    if (!found_actual) throw InvalidInputException("Column '%s' not found", actual_col_name);
    if (!found_forecast) throw InvalidInputException("Column '%s' not found", forecast_col_name);
    if (!found_baseline) throw InvalidInputException("Column '%s' not found", baseline_col_name);

    // Exclude date, actual, forecast, baseline from grouping
    for (idx_t i = 0; i < col_names.size(); i++) {
        if (i != bind_data->date_col_idx &&
            i != bind_data->actual_col_idx &&
            i != bind_data->forecast_col_idx &&
            i != bind_data->baseline_col_idx) {
            bind_data->group_col_indices.push_back(i);
            bind_data->output_col_names.push_back(col_names[i]);
            bind_data->output_col_types.push_back(col_types[i]);
            names.push_back(col_names[i]);
            return_types.push_back(col_types[i]);
        }
    }

    names.push_back("mase");
    return_types.push_back(LogicalType::DOUBLE);

    return bind_data;
}

static unique_ptr<GlobalTableFunctionState> TsMaseNativeInitGlobal(
    ClientContext &context, TableFunctionInitInput &input) {
    return make_uniq<TsMaseNativeGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsMaseNativeInitLocal(
    ExecutionContext &context, TableFunctionInitInput &input, GlobalTableFunctionState *global_state) {
    return make_uniq<TsMaseNativeLocalState>();
}

static OperatorResultType TsMaseNativeInOut(
    ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input, DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsMaseNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsMaseNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsMaseNativeLocalState>();

    if (!lstate.registered_collector) {
        gstate.threads_collecting.fetch_add(1);
        lstate.registered_collector = true;
    }

    // Extract batch locally first (no lock)
    struct LocalEntry {
        string group_key;
        vector<Value> group_vals;
        int64_t date_int;
        double actual_dbl;
        double forecast_dbl;
        double baseline_dbl;
    };
    vector<LocalEntry> batch;
    batch.reserve(input.size());

    for (idx_t i = 0; i < input.size(); i++) {
        vector<Value> group_vals;
        for (idx_t col_idx : bind_data.group_col_indices) {
            group_vals.push_back(input.data[col_idx].GetValue(i));
        }
        string group_key = BuildGroupKey(group_vals);

        Value date_val = input.data[bind_data.date_col_idx].GetValue(i);
        if (date_val.IsNull()) continue;

        Value actual_val = input.data[bind_data.actual_col_idx].GetValue(i);
        Value forecast_val = input.data[bind_data.forecast_col_idx].GetValue(i);
        Value baseline_val = input.data[bind_data.baseline_col_idx].GetValue(i);

        batch.push_back({
            std::move(group_key), std::move(group_vals),
            DateValueToInt64(date_val, bind_data.date_col_type),
            actual_val.IsNull() ? std::nan("") : actual_val.GetValue<double>(),
            forecast_val.IsNull() ? std::nan("") : forecast_val.GetValue<double>(),
            baseline_val.IsNull() ? std::nan("") : baseline_val.GetValue<double>()
        });
    }

    // Lock once and insert all
    {
        std::lock_guard<std::mutex> lock(gstate.groups_mutex);
        for (auto &entry : batch) {
            if (gstate.groups.find(entry.group_key) == gstate.groups.end()) {
                gstate.groups[entry.group_key] = MaseGroupData();
                gstate.groups[entry.group_key].group_values = entry.group_vals;
                gstate.group_order.push_back(entry.group_key);
            }

            auto &grp = gstate.groups[entry.group_key];
            grp.dates.push_back(entry.date_int);
            grp.actuals.push_back(entry.actual_dbl);
            grp.forecasts.push_back(entry.forecast_dbl);
            grp.baselines.push_back(entry.baseline_dbl);
        }
    }

    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

static OperatorFinalizeResultType TsMaseNativeFinalize(
    ExecutionContext &context, TableFunctionInput &data_p, DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsMaseNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsMaseNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsMaseNativeLocalState>();

    // Barrier + claim pattern
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

    if (!gstate.processed) {
        for (const auto &group_key : gstate.group_order) {
            auto &grp = gstate.groups[group_key];
            if (grp.dates.empty()) continue;

            vector<size_t> indices(grp.dates.size());
            for (size_t j = 0; j < indices.size(); j++) indices[j] = j;
            std::sort(indices.begin(), indices.end(),
                [&grp](size_t a, size_t b) { return grp.dates[a] < grp.dates[b]; });

            vector<double> sorted_actuals, sorted_forecasts, sorted_baselines;
            for (size_t j : indices) {
                if (!std::isnan(grp.actuals[j]) && !std::isnan(grp.forecasts[j]) && !std::isnan(grp.baselines[j])) {
                    sorted_actuals.push_back(grp.actuals[j]);
                    sorted_forecasts.push_back(grp.forecasts[j]);
                    sorted_baselines.push_back(grp.baselines[j]);
                }
            }

            double metric_val = std::nan("");
            if (!sorted_actuals.empty()) {
                AnofoxError error;
                double result;
                bool success = anofox_ts_mase(
                    sorted_actuals.data(), sorted_actuals.size(),
                    sorted_forecasts.data(), sorted_forecasts.size(),
                    sorted_baselines.data(), sorted_baselines.size(),
                    &result, &error);
                if (success) metric_val = result;
            }

            MaseOutputRow row;
            row.group_key = group_key;
            row.group_values = grp.group_values;
            row.metric_value = metric_val;
            gstate.results.push_back(std::move(row));
        }
        gstate.processed = true;
    }

    idx_t remaining = gstate.results.size() - gstate.output_offset;
    if (remaining == 0) {
        output.SetCardinality(0);
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t to_output = std::min(remaining, static_cast<idx_t>(STANDARD_VECTOR_SIZE));
    output.SetCardinality(to_output);

    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    for (idx_t i = 0; i < to_output; i++) {
        auto &row = gstate.results[gstate.output_offset + i];
        for (idx_t col = 0; col < row.group_values.size(); col++) {
            output.data[col].SetValue(i, row.group_values[col]);
        }
        output.data[row.group_values.size()].SetValue(i, Value::DOUBLE(row.metric_value));
    }

    gstate.output_offset += to_output;
    return (gstate.output_offset >= gstate.results.size())
        ? OperatorFinalizeResultType::FINISHED
        : OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

void RegisterTsMaseNativeFunction(ExtensionLoader &loader) {
    TableFunction func(
        "_ts_mase_native",
        {LogicalType::TABLE, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
        nullptr, TsMaseNativeBind, TsMaseNativeInitGlobal, TsMaseNativeInitLocal);
    func.in_out_function = TsMaseNativeInOut;
    func.in_out_function_final = TsMaseNativeFinalize;
    loader.RegisterFunction(func);
}

// ============================================================================
// ============================================================================
// _ts_rmae_native - rMAE metric with GROUP BY ALL
// ============================================================================
// ============================================================================

struct TsRmaeNativeBindData : public TableFunctionData {
    idx_t date_col_idx = 0;
    idx_t actual_col_idx = 0;
    idx_t pred1_col_idx = 0;
    idx_t pred2_col_idx = 0;

    vector<idx_t> group_col_indices;
    vector<string> output_col_names;
    vector<LogicalType> output_col_types;
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
};

// ============================================================================
// Standalone structs for TsRmaeNative
// ============================================================================

struct RmaeGroupData {
    vector<Value> group_values;
    vector<int64_t> dates;
    vector<double> actuals;
    vector<double> pred1s;
    vector<double> pred2s;
};

struct RmaeOutputRow {
    string group_key;
    vector<Value> group_values;
    double metric_value;
};

struct TsRmaeNativeGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override { return 999999; }

    std::mutex groups_mutex;
    std::map<string, RmaeGroupData> groups;
    vector<string> group_order;

    vector<RmaeOutputRow> results;
    bool processed = false;
    idx_t output_offset = 0;
    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};
};

struct TsRmaeNativeLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool registered_collector = false;
    bool registered_finalizer = false;
};

static unique_ptr<FunctionData> TsRmaeNativeBind(
    ClientContext &context, TableFunctionBindInput &input,
    vector<LogicalType> &return_types, vector<string> &names) {

    auto bind_data = make_uniq<TsRmaeNativeBindData>();

    if (input.inputs.size() < 5) {
        throw InvalidInputException(
            "_ts_rmae_native requires: (input_table, date_col, actual_col, pred1_col, pred2_col)");
    }

    string date_col_name = input.inputs[1].GetValue<string>();
    string actual_col_name = input.inputs[2].GetValue<string>();
    string pred1_col_name = input.inputs[3].GetValue<string>();
    string pred2_col_name = input.inputs[4].GetValue<string>();

    auto &col_names = input.input_table_names;
    auto &col_types = input.input_table_types;

    bool found_date = false, found_actual = false, found_pred1 = false, found_pred2 = false;

    for (idx_t i = 0; i < col_names.size(); i++) {
        if (col_names[i] == date_col_name) {
            bind_data->date_col_idx = i;
            found_date = true;
            auto &dtype = col_types[i];
            if (dtype.id() == LogicalTypeId::DATE) bind_data->date_col_type = DateColumnType::DATE;
            else if (dtype.id() == LogicalTypeId::TIMESTAMP) bind_data->date_col_type = DateColumnType::TIMESTAMP;
            else if (dtype.id() == LogicalTypeId::INTEGER) bind_data->date_col_type = DateColumnType::INTEGER;
            else if (dtype.id() == LogicalTypeId::BIGINT) bind_data->date_col_type = DateColumnType::BIGINT;
        } else if (col_names[i] == actual_col_name) {
            bind_data->actual_col_idx = i; found_actual = true;
        } else if (col_names[i] == pred1_col_name) {
            bind_data->pred1_col_idx = i; found_pred1 = true;
        } else if (col_names[i] == pred2_col_name) {
            bind_data->pred2_col_idx = i; found_pred2 = true;
        }
    }

    if (!found_date) throw InvalidInputException("Column '%s' not found", date_col_name);
    if (!found_actual) throw InvalidInputException("Column '%s' not found", actual_col_name);
    if (!found_pred1) throw InvalidInputException("Column '%s' not found", pred1_col_name);
    if (!found_pred2) throw InvalidInputException("Column '%s' not found", pred2_col_name);

    for (idx_t i = 0; i < col_names.size(); i++) {
        if (i != bind_data->date_col_idx && i != bind_data->actual_col_idx &&
            i != bind_data->pred1_col_idx && i != bind_data->pred2_col_idx) {
            bind_data->group_col_indices.push_back(i);
            names.push_back(col_names[i]);
            return_types.push_back(col_types[i]);
        }
    }

    names.push_back("rmae");
    return_types.push_back(LogicalType::DOUBLE);

    return bind_data;
}

static unique_ptr<GlobalTableFunctionState> TsRmaeNativeInitGlobal(
    ClientContext &context, TableFunctionInitInput &input) {
    return make_uniq<TsRmaeNativeGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsRmaeNativeInitLocal(
    ExecutionContext &context, TableFunctionInitInput &input, GlobalTableFunctionState *global_state) {
    return make_uniq<TsRmaeNativeLocalState>();
}

static OperatorResultType TsRmaeNativeInOut(
    ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input, DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsRmaeNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsRmaeNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsRmaeNativeLocalState>();

    if (!lstate.registered_collector) {
        gstate.threads_collecting.fetch_add(1);
        lstate.registered_collector = true;
    }

    // Extract batch locally first (no lock)
    struct LocalEntry {
        string group_key;
        vector<Value> group_vals;
        int64_t date_int;
        double actual_dbl;
        double pred1_dbl;
        double pred2_dbl;
    };
    vector<LocalEntry> batch;
    batch.reserve(input.size());

    for (idx_t i = 0; i < input.size(); i++) {
        vector<Value> group_vals;
        for (idx_t col_idx : bind_data.group_col_indices) {
            group_vals.push_back(input.data[col_idx].GetValue(i));
        }
        string group_key = BuildGroupKey(group_vals);

        Value date_val = input.data[bind_data.date_col_idx].GetValue(i);
        if (date_val.IsNull()) continue;

        Value actual_val = input.data[bind_data.actual_col_idx].GetValue(i);
        Value pred1_val = input.data[bind_data.pred1_col_idx].GetValue(i);
        Value pred2_val = input.data[bind_data.pred2_col_idx].GetValue(i);

        batch.push_back({
            std::move(group_key), std::move(group_vals),
            DateValueToInt64(date_val, bind_data.date_col_type),
            actual_val.IsNull() ? std::nan("") : actual_val.GetValue<double>(),
            pred1_val.IsNull() ? std::nan("") : pred1_val.GetValue<double>(),
            pred2_val.IsNull() ? std::nan("") : pred2_val.GetValue<double>()
        });
    }

    // Lock once and insert all
    {
        std::lock_guard<std::mutex> lock(gstate.groups_mutex);
        for (auto &entry : batch) {
            if (gstate.groups.find(entry.group_key) == gstate.groups.end()) {
                gstate.groups[entry.group_key] = RmaeGroupData();
                gstate.groups[entry.group_key].group_values = entry.group_vals;
                gstate.group_order.push_back(entry.group_key);
            }

            auto &grp = gstate.groups[entry.group_key];
            grp.dates.push_back(entry.date_int);
            grp.actuals.push_back(entry.actual_dbl);
            grp.pred1s.push_back(entry.pred1_dbl);
            grp.pred2s.push_back(entry.pred2_dbl);
        }
    }

    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

static OperatorFinalizeResultType TsRmaeNativeFinalize(
    ExecutionContext &context, TableFunctionInput &data_p, DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsRmaeNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsRmaeNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsRmaeNativeLocalState>();

    // Barrier + claim pattern
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

    if (!gstate.processed) {
        for (const auto &group_key : gstate.group_order) {
            auto &grp = gstate.groups[group_key];
            if (grp.dates.empty()) continue;

            vector<size_t> indices(grp.dates.size());
            for (size_t j = 0; j < indices.size(); j++) indices[j] = j;
            std::sort(indices.begin(), indices.end(),
                [&grp](size_t a, size_t b) { return grp.dates[a] < grp.dates[b]; });

            vector<double> sorted_actuals, sorted_pred1s, sorted_pred2s;
            for (size_t j : indices) {
                if (!std::isnan(grp.actuals[j]) && !std::isnan(grp.pred1s[j]) && !std::isnan(grp.pred2s[j])) {
                    sorted_actuals.push_back(grp.actuals[j]);
                    sorted_pred1s.push_back(grp.pred1s[j]);
                    sorted_pred2s.push_back(grp.pred2s[j]);
                }
            }

            double metric_val = std::nan("");
            if (!sorted_actuals.empty()) {
                AnofoxError error;
                double result;
                bool success = anofox_ts_rmae(
                    sorted_actuals.data(), sorted_actuals.size(),
                    sorted_pred1s.data(), sorted_pred1s.size(),
                    sorted_pred2s.data(), sorted_pred2s.size(),
                    &result, &error);
                if (success) metric_val = result;
            }

            RmaeOutputRow row;
            row.group_key = group_key;
            row.group_values = grp.group_values;
            row.metric_value = metric_val;
            gstate.results.push_back(std::move(row));
        }
        gstate.processed = true;
    }

    idx_t remaining = gstate.results.size() - gstate.output_offset;
    if (remaining == 0) {
        output.SetCardinality(0);
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t to_output = std::min(remaining, static_cast<idx_t>(STANDARD_VECTOR_SIZE));
    output.SetCardinality(to_output);

    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    for (idx_t i = 0; i < to_output; i++) {
        auto &row = gstate.results[gstate.output_offset + i];
        for (idx_t col = 0; col < row.group_values.size(); col++) {
            output.data[col].SetValue(i, row.group_values[col]);
        }
        output.data[row.group_values.size()].SetValue(i, Value::DOUBLE(row.metric_value));
    }

    gstate.output_offset += to_output;
    return (gstate.output_offset >= gstate.results.size())
        ? OperatorFinalizeResultType::FINISHED
        : OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

void RegisterTsRmaeNativeFunction(ExtensionLoader &loader) {
    TableFunction func(
        "_ts_rmae_native",
        {LogicalType::TABLE, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
        nullptr, TsRmaeNativeBind, TsRmaeNativeInitGlobal, TsRmaeNativeInitLocal);
    func.in_out_function = TsRmaeNativeInOut;
    func.in_out_function_final = TsRmaeNativeFinalize;
    loader.RegisterFunction(func);
}

// ============================================================================
// ============================================================================
// _ts_coverage_native - Coverage metric with GROUP BY ALL
// ============================================================================
// ============================================================================

struct TsCoverageNativeBindData : public TableFunctionData {
    idx_t date_col_idx = 0;
    idx_t actual_col_idx = 0;
    idx_t lower_col_idx = 0;
    idx_t upper_col_idx = 0;

    vector<idx_t> group_col_indices;
    vector<string> output_col_names;
    vector<LogicalType> output_col_types;
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
};

// ============================================================================
// Standalone structs for TsCoverageNative
// ============================================================================

struct CoverageGroupData {
    vector<Value> group_values;
    vector<int64_t> dates;
    vector<double> actuals;
    vector<double> lowers;
    vector<double> uppers;
};

struct CoverageOutputRow {
    string group_key;
    vector<Value> group_values;
    double metric_value;
};

struct TsCoverageNativeGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override { return 999999; }

    std::mutex groups_mutex;
    std::map<string, CoverageGroupData> groups;
    vector<string> group_order;

    vector<CoverageOutputRow> results;
    bool processed = false;
    idx_t output_offset = 0;
    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};
};

struct TsCoverageNativeLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool registered_collector = false;
    bool registered_finalizer = false;
};

static unique_ptr<FunctionData> TsCoverageNativeBind(
    ClientContext &context, TableFunctionBindInput &input,
    vector<LogicalType> &return_types, vector<string> &names) {

    auto bind_data = make_uniq<TsCoverageNativeBindData>();

    if (input.inputs.size() < 5) {
        throw InvalidInputException(
            "_ts_coverage_native requires: (input_table, date_col, actual_col, lower_col, upper_col)");
    }

    string date_col_name = input.inputs[1].GetValue<string>();
    string actual_col_name = input.inputs[2].GetValue<string>();
    string lower_col_name = input.inputs[3].GetValue<string>();
    string upper_col_name = input.inputs[4].GetValue<string>();

    auto &col_names = input.input_table_names;
    auto &col_types = input.input_table_types;

    bool found_date = false, found_actual = false, found_lower = false, found_upper = false;

    for (idx_t i = 0; i < col_names.size(); i++) {
        if (col_names[i] == date_col_name) {
            bind_data->date_col_idx = i;
            found_date = true;
            auto &dtype = col_types[i];
            if (dtype.id() == LogicalTypeId::DATE) bind_data->date_col_type = DateColumnType::DATE;
            else if (dtype.id() == LogicalTypeId::TIMESTAMP) bind_data->date_col_type = DateColumnType::TIMESTAMP;
            else if (dtype.id() == LogicalTypeId::INTEGER) bind_data->date_col_type = DateColumnType::INTEGER;
            else if (dtype.id() == LogicalTypeId::BIGINT) bind_data->date_col_type = DateColumnType::BIGINT;
        } else if (col_names[i] == actual_col_name) {
            bind_data->actual_col_idx = i; found_actual = true;
        } else if (col_names[i] == lower_col_name) {
            bind_data->lower_col_idx = i; found_lower = true;
        } else if (col_names[i] == upper_col_name) {
            bind_data->upper_col_idx = i; found_upper = true;
        }
    }

    if (!found_date) throw InvalidInputException("Column '%s' not found", date_col_name);
    if (!found_actual) throw InvalidInputException("Column '%s' not found", actual_col_name);
    if (!found_lower) throw InvalidInputException("Column '%s' not found", lower_col_name);
    if (!found_upper) throw InvalidInputException("Column '%s' not found", upper_col_name);

    for (idx_t i = 0; i < col_names.size(); i++) {
        if (i != bind_data->date_col_idx && i != bind_data->actual_col_idx &&
            i != bind_data->lower_col_idx && i != bind_data->upper_col_idx) {
            bind_data->group_col_indices.push_back(i);
            names.push_back(col_names[i]);
            return_types.push_back(col_types[i]);
        }
    }

    names.push_back("coverage");
    return_types.push_back(LogicalType::DOUBLE);

    return bind_data;
}

static unique_ptr<GlobalTableFunctionState> TsCoverageNativeInitGlobal(
    ClientContext &context, TableFunctionInitInput &input) {
    return make_uniq<TsCoverageNativeGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsCoverageNativeInitLocal(
    ExecutionContext &context, TableFunctionInitInput &input, GlobalTableFunctionState *global_state) {
    return make_uniq<TsCoverageNativeLocalState>();
}

static OperatorResultType TsCoverageNativeInOut(
    ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input, DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsCoverageNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsCoverageNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsCoverageNativeLocalState>();

    if (!lstate.registered_collector) {
        gstate.threads_collecting.fetch_add(1);
        lstate.registered_collector = true;
    }

    // Extract batch locally first (no lock)
    struct LocalEntry {
        string group_key;
        vector<Value> group_vals;
        int64_t date_int;
        double actual_dbl;
        double lower_dbl;
        double upper_dbl;
    };
    vector<LocalEntry> batch;
    batch.reserve(input.size());

    for (idx_t i = 0; i < input.size(); i++) {
        vector<Value> group_vals;
        for (idx_t col_idx : bind_data.group_col_indices) {
            group_vals.push_back(input.data[col_idx].GetValue(i));
        }
        string group_key = BuildGroupKey(group_vals);

        Value date_val = input.data[bind_data.date_col_idx].GetValue(i);
        if (date_val.IsNull()) continue;

        Value actual_val = input.data[bind_data.actual_col_idx].GetValue(i);
        Value lower_val = input.data[bind_data.lower_col_idx].GetValue(i);
        Value upper_val = input.data[bind_data.upper_col_idx].GetValue(i);

        batch.push_back({
            std::move(group_key), std::move(group_vals),
            DateValueToInt64(date_val, bind_data.date_col_type),
            actual_val.IsNull() ? std::nan("") : actual_val.GetValue<double>(),
            lower_val.IsNull() ? std::nan("") : lower_val.GetValue<double>(),
            upper_val.IsNull() ? std::nan("") : upper_val.GetValue<double>()
        });
    }

    // Lock once and insert all
    {
        std::lock_guard<std::mutex> lock(gstate.groups_mutex);
        for (auto &entry : batch) {
            if (gstate.groups.find(entry.group_key) == gstate.groups.end()) {
                gstate.groups[entry.group_key] = CoverageGroupData();
                gstate.groups[entry.group_key].group_values = entry.group_vals;
                gstate.group_order.push_back(entry.group_key);
            }

            auto &grp = gstate.groups[entry.group_key];
            grp.dates.push_back(entry.date_int);
            grp.actuals.push_back(entry.actual_dbl);
            grp.lowers.push_back(entry.lower_dbl);
            grp.uppers.push_back(entry.upper_dbl);
        }
    }

    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

static OperatorFinalizeResultType TsCoverageNativeFinalize(
    ExecutionContext &context, TableFunctionInput &data_p, DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsCoverageNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsCoverageNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsCoverageNativeLocalState>();

    // Barrier + claim pattern
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

    if (!gstate.processed) {
        for (const auto &group_key : gstate.group_order) {
            auto &grp = gstate.groups[group_key];
            if (grp.dates.empty()) continue;

            vector<size_t> indices(grp.dates.size());
            for (size_t j = 0; j < indices.size(); j++) indices[j] = j;
            std::sort(indices.begin(), indices.end(),
                [&grp](size_t a, size_t b) { return grp.dates[a] < grp.dates[b]; });

            vector<double> sorted_actuals, sorted_lowers, sorted_uppers;
            for (size_t j : indices) {
                if (!std::isnan(grp.actuals[j]) && !std::isnan(grp.lowers[j]) && !std::isnan(grp.uppers[j])) {
                    sorted_actuals.push_back(grp.actuals[j]);
                    sorted_lowers.push_back(grp.lowers[j]);
                    sorted_uppers.push_back(grp.uppers[j]);
                }
            }

            double metric_val = std::nan("");
            if (!sorted_actuals.empty()) {
                AnofoxError error;
                double result;
                bool success = anofox_ts_coverage(
                    sorted_actuals.data(), sorted_actuals.size(),
                    sorted_lowers.data(),
                    sorted_uppers.data(),
                    &result, &error);
                if (success) metric_val = result;
            }

            CoverageOutputRow row;
            row.group_key = group_key;
            row.group_values = grp.group_values;
            row.metric_value = metric_val;
            gstate.results.push_back(std::move(row));
        }
        gstate.processed = true;
    }

    idx_t remaining = gstate.results.size() - gstate.output_offset;
    if (remaining == 0) {
        output.SetCardinality(0);
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t to_output = std::min(remaining, static_cast<idx_t>(STANDARD_VECTOR_SIZE));
    output.SetCardinality(to_output);

    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    for (idx_t i = 0; i < to_output; i++) {
        auto &row = gstate.results[gstate.output_offset + i];
        for (idx_t col = 0; col < row.group_values.size(); col++) {
            output.data[col].SetValue(i, row.group_values[col]);
        }
        output.data[row.group_values.size()].SetValue(i, Value::DOUBLE(row.metric_value));
    }

    gstate.output_offset += to_output;
    return (gstate.output_offset >= gstate.results.size())
        ? OperatorFinalizeResultType::FINISHED
        : OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

void RegisterTsCoverageNativeFunction(ExtensionLoader &loader) {
    TableFunction func(
        "_ts_coverage_native",
        {LogicalType::TABLE, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
        nullptr, TsCoverageNativeBind, TsCoverageNativeInitGlobal, TsCoverageNativeInitLocal);
    func.in_out_function = TsCoverageNativeInOut;
    func.in_out_function_final = TsCoverageNativeFinalize;
    loader.RegisterFunction(func);
}

// ============================================================================
// ============================================================================
// _ts_quantile_loss_native - Quantile Loss metric with GROUP BY ALL
// ============================================================================
// ============================================================================

struct TsQuantileLossNativeBindData : public TableFunctionData {
    idx_t date_col_idx = 0;
    idx_t actual_col_idx = 0;
    idx_t forecast_col_idx = 0;
    double quantile = 0.5;

    vector<idx_t> group_col_indices;
    vector<string> output_col_names;
    vector<LogicalType> output_col_types;
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
};

// ============================================================================
// Standalone structs for TsQuantileLossNative
// ============================================================================

struct QuantileLossGroupData {
    vector<Value> group_values;
    vector<int64_t> dates;
    vector<double> actuals;
    vector<double> forecasts;
};

struct QuantileLossOutputRow {
    string group_key;
    vector<Value> group_values;
    double metric_value;
};

struct TsQuantileLossNativeGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override { return 999999; }

    std::mutex groups_mutex;
    std::map<string, QuantileLossGroupData> groups;
    vector<string> group_order;

    vector<QuantileLossOutputRow> results;
    bool processed = false;
    idx_t output_offset = 0;
    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};
};

struct TsQuantileLossNativeLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool registered_collector = false;
    bool registered_finalizer = false;
};

static unique_ptr<FunctionData> TsQuantileLossNativeBind(
    ClientContext &context, TableFunctionBindInput &input,
    vector<LogicalType> &return_types, vector<string> &names) {

    auto bind_data = make_uniq<TsQuantileLossNativeBindData>();

    if (input.inputs.size() < 5) {
        throw InvalidInputException(
            "_ts_quantile_loss_native requires: (input_table, date_col, actual_col, forecast_col, quantile)");
    }

    string date_col_name = input.inputs[1].GetValue<string>();
    string actual_col_name = input.inputs[2].GetValue<string>();
    string forecast_col_name = input.inputs[3].GetValue<string>();
    bind_data->quantile = input.inputs[4].GetValue<double>();

    auto &col_names = input.input_table_names;
    auto &col_types = input.input_table_types;

    bool found_date = false, found_actual = false, found_forecast = false;

    for (idx_t i = 0; i < col_names.size(); i++) {
        if (col_names[i] == date_col_name) {
            bind_data->date_col_idx = i;
            found_date = true;
            auto &dtype = col_types[i];
            if (dtype.id() == LogicalTypeId::DATE) bind_data->date_col_type = DateColumnType::DATE;
            else if (dtype.id() == LogicalTypeId::TIMESTAMP) bind_data->date_col_type = DateColumnType::TIMESTAMP;
            else if (dtype.id() == LogicalTypeId::INTEGER) bind_data->date_col_type = DateColumnType::INTEGER;
            else if (dtype.id() == LogicalTypeId::BIGINT) bind_data->date_col_type = DateColumnType::BIGINT;
        } else if (col_names[i] == actual_col_name) {
            bind_data->actual_col_idx = i; found_actual = true;
        } else if (col_names[i] == forecast_col_name) {
            bind_data->forecast_col_idx = i; found_forecast = true;
        }
    }

    if (!found_date) throw InvalidInputException("Column '%s' not found", date_col_name);
    if (!found_actual) throw InvalidInputException("Column '%s' not found", actual_col_name);
    if (!found_forecast) throw InvalidInputException("Column '%s' not found", forecast_col_name);

    for (idx_t i = 0; i < col_names.size(); i++) {
        if (i != bind_data->date_col_idx && i != bind_data->actual_col_idx &&
            i != bind_data->forecast_col_idx) {
            bind_data->group_col_indices.push_back(i);
            names.push_back(col_names[i]);
            return_types.push_back(col_types[i]);
        }
    }

    names.push_back("quantile_loss");
    return_types.push_back(LogicalType::DOUBLE);

    return bind_data;
}

static unique_ptr<GlobalTableFunctionState> TsQuantileLossNativeInitGlobal(
    ClientContext &context, TableFunctionInitInput &input) {
    return make_uniq<TsQuantileLossNativeGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsQuantileLossNativeInitLocal(
    ExecutionContext &context, TableFunctionInitInput &input, GlobalTableFunctionState *global_state) {
    return make_uniq<TsQuantileLossNativeLocalState>();
}

static OperatorResultType TsQuantileLossNativeInOut(
    ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input, DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsQuantileLossNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsQuantileLossNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsQuantileLossNativeLocalState>();

    if (!lstate.registered_collector) {
        gstate.threads_collecting.fetch_add(1);
        lstate.registered_collector = true;
    }

    // Extract batch locally first (no lock)
    struct LocalEntry {
        string group_key;
        vector<Value> group_vals;
        int64_t date_int;
        double actual_dbl;
        double forecast_dbl;
    };
    vector<LocalEntry> batch;
    batch.reserve(input.size());

    for (idx_t i = 0; i < input.size(); i++) {
        vector<Value> group_vals;
        for (idx_t col_idx : bind_data.group_col_indices) {
            group_vals.push_back(input.data[col_idx].GetValue(i));
        }
        string group_key = BuildGroupKey(group_vals);

        Value date_val = input.data[bind_data.date_col_idx].GetValue(i);
        if (date_val.IsNull()) continue;

        Value actual_val = input.data[bind_data.actual_col_idx].GetValue(i);
        Value forecast_val = input.data[bind_data.forecast_col_idx].GetValue(i);

        batch.push_back({
            std::move(group_key), std::move(group_vals),
            DateValueToInt64(date_val, bind_data.date_col_type),
            actual_val.IsNull() ? std::nan("") : actual_val.GetValue<double>(),
            forecast_val.IsNull() ? std::nan("") : forecast_val.GetValue<double>()
        });
    }

    // Lock once and insert all
    {
        std::lock_guard<std::mutex> lock(gstate.groups_mutex);
        for (auto &entry : batch) {
            if (gstate.groups.find(entry.group_key) == gstate.groups.end()) {
                gstate.groups[entry.group_key] = QuantileLossGroupData();
                gstate.groups[entry.group_key].group_values = entry.group_vals;
                gstate.group_order.push_back(entry.group_key);
            }

            auto &grp = gstate.groups[entry.group_key];
            grp.dates.push_back(entry.date_int);
            grp.actuals.push_back(entry.actual_dbl);
            grp.forecasts.push_back(entry.forecast_dbl);
        }
    }

    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

static OperatorFinalizeResultType TsQuantileLossNativeFinalize(
    ExecutionContext &context, TableFunctionInput &data_p, DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsQuantileLossNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsQuantileLossNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsQuantileLossNativeLocalState>();

    // Barrier + claim pattern
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

    if (!gstate.processed) {
        for (const auto &group_key : gstate.group_order) {
            auto &grp = gstate.groups[group_key];
            if (grp.dates.empty()) continue;

            vector<size_t> indices(grp.dates.size());
            for (size_t j = 0; j < indices.size(); j++) indices[j] = j;
            std::sort(indices.begin(), indices.end(),
                [&grp](size_t a, size_t b) { return grp.dates[a] < grp.dates[b]; });

            vector<double> sorted_actuals, sorted_forecasts;
            for (size_t j : indices) {
                if (!std::isnan(grp.actuals[j]) && !std::isnan(grp.forecasts[j])) {
                    sorted_actuals.push_back(grp.actuals[j]);
                    sorted_forecasts.push_back(grp.forecasts[j]);
                }
            }

            double metric_val = std::nan("");
            if (!sorted_actuals.empty()) {
                AnofoxError error;
                double result;
                bool success = anofox_ts_quantile_loss(
                    sorted_actuals.data(), sorted_actuals.size(),
                    sorted_forecasts.data(), sorted_forecasts.size(),
                    bind_data.quantile,
                    &result, &error);
                if (success) metric_val = result;
            }

            QuantileLossOutputRow row;
            row.group_key = group_key;
            row.group_values = grp.group_values;
            row.metric_value = metric_val;
            gstate.results.push_back(std::move(row));
        }
        gstate.processed = true;
    }

    idx_t remaining = gstate.results.size() - gstate.output_offset;
    if (remaining == 0) {
        output.SetCardinality(0);
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t to_output = std::min(remaining, static_cast<idx_t>(STANDARD_VECTOR_SIZE));
    output.SetCardinality(to_output);

    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    for (idx_t i = 0; i < to_output; i++) {
        auto &row = gstate.results[gstate.output_offset + i];
        for (idx_t col = 0; col < row.group_values.size(); col++) {
            output.data[col].SetValue(i, row.group_values[col]);
        }
        output.data[row.group_values.size()].SetValue(i, Value::DOUBLE(row.metric_value));
    }

    gstate.output_offset += to_output;
    return (gstate.output_offset >= gstate.results.size())
        ? OperatorFinalizeResultType::FINISHED
        : OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

void RegisterTsQuantileLossNativeFunction(ExtensionLoader &loader) {
    TableFunction func(
        "_ts_quantile_loss_native",
        {LogicalType::TABLE, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::DOUBLE},
        nullptr, TsQuantileLossNativeBind, TsQuantileLossNativeInitGlobal, TsQuantileLossNativeInitLocal);
    func.in_out_function = TsQuantileLossNativeInOut;
    func.in_out_function_final = TsQuantileLossNativeFinalize;
    loader.RegisterFunction(func);
}

} // namespace duckdb
