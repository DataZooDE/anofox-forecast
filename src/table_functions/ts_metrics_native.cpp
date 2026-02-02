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
// Global State
// ============================================================================

struct TsMetricsNativeGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override {
        return 999999;
    }

    std::mutex processed_groups_mutex;
    std::set<string> processed_groups;

    bool ClaimGroup(const string &group_key) {
        std::lock_guard<std::mutex> lock(processed_groups_mutex);
        auto result = processed_groups.insert(group_key);
        return result.second;
    }
};

// ============================================================================
// Local State
// ============================================================================

struct TsMetricsNativeLocalState : public LocalTableFunctionState {
    // Data per group
    struct GroupData {
        vector<Value> group_values;  // Values for grouping columns
        vector<int64_t> dates;       // For ordering
        vector<double> actuals;
        vector<double> forecasts;
    };

    std::map<string, GroupData> groups;
    vector<string> group_order;

    // Output buffer
    struct OutputRow {
        string group_key;
        vector<Value> group_values;
        double metric_value;
    };
    vector<OutputRow> results;

    bool processed = false;
    idx_t output_offset = 0;
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
    auto &local_state = data_p.local_state->Cast<TsMetricsNativeLocalState>();

    // Buffer incoming data
    for (idx_t i = 0; i < input.size(); i++) {
        // Extract group column values
        vector<Value> group_vals;
        for (idx_t col_idx : bind_data.group_col_indices) {
            group_vals.push_back(input.data[col_idx].GetValue(i));
        }

        string group_key = BuildGroupKey(group_vals);

        // Get date, actual, forecast values
        Value date_val = input.data[bind_data.date_col_idx].GetValue(i);
        Value actual_val = input.data[bind_data.actual_col_idx].GetValue(i);
        Value forecast_val = input.data[bind_data.forecast_col_idx].GetValue(i);

        if (date_val.IsNull()) continue;

        // Initialize group if needed
        if (local_state.groups.find(group_key) == local_state.groups.end()) {
            local_state.groups[group_key] = TsMetricsNativeLocalState::GroupData();
            local_state.groups[group_key].group_values = group_vals;
            local_state.group_order.push_back(group_key);
        }

        auto &grp = local_state.groups[group_key];

        int64_t date_int = DateValueToInt64(date_val, bind_data.date_col_type);
        double actual_dbl = actual_val.IsNull() ? std::nan("") : actual_val.GetValue<double>();
        double forecast_dbl = forecast_val.IsNull() ? std::nan("") : forecast_val.GetValue<double>();

        grp.dates.push_back(date_int);
        grp.actuals.push_back(actual_dbl);
        grp.forecasts.push_back(forecast_dbl);
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
    auto &global_state = data_p.global_state->Cast<TsMetricsNativeGlobalState>();
    auto &local_state = data_p.local_state->Cast<TsMetricsNativeLocalState>();

    // Process all groups on first finalize call
    if (!local_state.processed) {
        for (const auto &group_key : local_state.group_order) {
            // Skip if another thread claimed this group
            if (!global_state.ClaimGroup(group_key)) {
                continue;
            }

            auto &grp = local_state.groups[group_key];

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
            TsMetricsNativeLocalState::OutputRow row;
            row.group_key = group_key;
            row.group_values = grp.group_values;
            row.metric_value = metric_val;
            local_state.results.push_back(std::move(row));
        }

        local_state.processed = true;
    }

    // Stream results
    idx_t remaining = local_state.results.size() - local_state.output_offset;
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
        auto &row = local_state.results[local_state.output_offset + i];

        // Output group columns
        for (idx_t col = 0; col < row.group_values.size(); col++) {
            output.data[col].SetValue(i, row.group_values[col]);
        }

        // Output metric value
        idx_t metric_col = row.group_values.size();
        output.data[metric_col].SetValue(i, Value::DOUBLE(row.metric_value));
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

} // namespace duckdb
