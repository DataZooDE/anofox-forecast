#include "duckdb_compat.hpp"
#include "ts_forecast_panel_native.hpp"
#include "ts_fill_gaps_native.hpp"  // ParseFrequencyWithType, date helpers, DateColumnType
#include "ts_future_dates.hpp"     // StepFromFrequency, ComputeFutureDates (D-12)
#include "anofox_fcst_ffi.h"
#include "ts_collect_finalize.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <thread>
#include <cmath>
#include <cstring>
#include <limits>

namespace duckdb {

// ============================================================================
// _ts_forecast_panel_native — Internal table function for panel forecasting.
//
// Collects all rows in-memory (same Finalize barrier as _ts_forecast_native),
// aligns the ragged panel to a shared date grid, drops invalid series
// (surfaced as DROPPED rows), then makes ONE anofox_ts_forecast_panel call
// across all kept series (fit-once-emit-many global model).
//
// Users should call ts_forecast_panel_by() instead of this function directly.
// ============================================================================

// ============================================================================
// Bind Data
// ============================================================================

struct TsForecastPanelNativeBindData : public TableFunctionData {
    int64_t horizon = 7;
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;
    FrequencyType frequency_type = FrequencyType::FIXED;

    // Panel model parameters
    string method = "GlobalETS";
    int64_t seasonal_period = 0;
    string model_pool = "";       // "Reduced" (default) | "Complete"
    string croston_variant = "";  // "Classic" (default) | "SBA" — reserved for 02-2

    // Type preservation
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
};

// ============================================================================
// Group Data and Result Structures
// ============================================================================

struct ForecastGroupData {
    Value group_value;
    vector<int64_t> dates;   // microseconds
    vector<double> values;
    vector<bool> validity;
};

struct PanelOutputRow {
    string group_key;
    Value group_value;
    int64_t forecast_step;
    int64_t date;        // microseconds (ignored when date_null == true)
    bool date_null;      // true → emit NULL for the date column
    double point_forecast;
    string model_name;
};

// ============================================================================
// Local State — per-thread flags only
// ============================================================================

struct TsForecastPanelNativeLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool arrived_at_final = false;
};

// ============================================================================
// Global State — thread-safe collection + single-thread finalize
// ============================================================================

struct TsForecastPanelNativeGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override { return 999999; }

    std::mutex groups_mutex;
    std::map<string, ForecastGroupData> groups;
    vector<string> group_order;

    vector<PanelOutputRow> results;
    bool processed = false;
    idx_t output_offset = 0;
    string deferred_error_message;  // set in n_kept<3 branch; thrown after DROPPED rows are flushed

    // Single-thread finalize: last-finalizer barrier (ts_collect_finalize.hpp)
    TsCollectFinalizeBarrier barrier;
};

// ============================================================================
// Param Helpers (mirrors ts_forecast_native.cpp)
// ============================================================================

static string ParseStringFromPanelParams(const Value &params_value, const string &key, const string &default_val) {
    if (params_value.IsNull()) {
        return default_val;
    }
    if (params_value.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params_value);
        for (auto &child : map_children) {
            auto &k = StructValue::GetChildren(child)[0];
            auto &v = StructValue::GetChildren(child)[1];
            if (k.ToString() == key && !v.IsNull()) {
                return v.ToString();
            }
        }
    } else if (params_value.type().id() == LogicalTypeId::STRUCT) {
        auto &struct_children = StructValue::GetChildren(params_value);
        auto &child_types = StructType::GetChildTypes(params_value.type());
        for (idx_t i = 0; i < child_types.size(); i++) {
            if (NameString(child_types[i].first) == key && !struct_children[i].IsNull()) {
                return struct_children[i].ToString();
            }
        }
    }
    return default_val;
}

static int64_t ParseInt64FromPanelParams(const Value &params_value, const string &key, int64_t default_val) {
    if (params_value.IsNull()) {
        return default_val;
    }
    if (params_value.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params_value);
        for (auto &child : map_children) {
            auto &k = StructValue::GetChildren(child)[0];
            auto &v = StructValue::GetChildren(child)[1];
            if (k.ToString() == key && !v.IsNull()) {
                try { return std::stoll(v.ToString()); } catch (...) { return default_val; }
            }
        }
    } else if (params_value.type().id() == LogicalTypeId::STRUCT) {
        auto &struct_children = StructValue::GetChildren(params_value);
        auto &child_types = StructType::GetChildTypes(params_value.type());
        for (idx_t i = 0; i < child_types.size(); i++) {
            if (NameString(child_types[i].first) == key && !struct_children[i].IsNull()) {
                try { return struct_children[i].GetValue<int64_t>(); }
                catch (...) {
                    try { return std::stoll(struct_children[i].ToString()); }
                    catch (...) { return default_val; }
                }
            }
        }
    }
    return default_val;
}

static void ValidatePanelParamKeys(const Value &params_value) {
    static const std::set<string> valid_keys = {
        "seasonal_period", "model_pool", "croston_variant"
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
            if (valid_keys.find(NameString(child_types[i].first)) == valid_keys.end()) {
                unknown_keys.push_back(NameString(child_types[i].first));
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
            "Unknown parameter(s) for ts_forecast_panel_by: %s. "
            "Valid parameters are: seasonal_period, model_pool, croston_variant",
            unknown_list);
    }
}

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsForecastPanelNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    TableBindNames &names) {

    auto bind_data = make_uniq<TsForecastPanelNativeBindData>();

    // Positional args: TABLE, horizon (INT), frequency (VARCHAR), method (VARCHAR), params (ANY)
    // Parse horizon (index 1)
    if (input.inputs.size() >= 2) {
        bind_data->horizon = input.inputs[1].GetValue<int64_t>();
    }

    // Parse frequency (index 2)
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
        ValidatePanelParamKeys(params);
        bind_data->seasonal_period = ParseInt64FromPanelParams(params, "seasonal_period", 0);
        bind_data->model_pool = ParseStringFromPanelParams(params, "model_pool", "");
        bind_data->croston_variant = ParseStringFromPanelParams(params, "croston_variant", "");
    }

    // Detect column types from input table
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

    // Output schema: <group_col>, forecast_step, <date_col>, yhat, model_name
    auto &table_names = input.input_table_names;
    string group_col_name = table_names.size() > 0 ? NameString(table_names[0]) : "id";
    string date_col_name = table_names.size() > 1 ? NameString(table_names[1]) : "date";

    names.push_back(MakeBindName(group_col_name));
    return_types.push_back(bind_data->group_logical_type);

    names.push_back(MakeBindName("forecast_step"));
    return_types.push_back(LogicalType::INTEGER);

    names.push_back(MakeBindName(date_col_name));
    return_types.push_back(bind_data->date_logical_type);

    names.push_back(MakeBindName("yhat"));
    return_types.push_back(LogicalType::DOUBLE);

    names.push_back(MakeBindName("model_name"));
    return_types.push_back(LogicalType::VARCHAR);

    return bind_data;
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsForecastPanelNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<TsForecastPanelNativeGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsForecastPanelNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    global_state->Cast<TsForecastPanelNativeGlobalState>().barrier.RegisterLocal();
    return make_uniq<TsForecastPanelNativeLocalState>();
}

// ============================================================================
// In-Out Function — buffers incoming rows (mirrors ts_forecast_native.cpp)
// ============================================================================

static OperatorResultType TsForecastPanelNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsForecastPanelNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsForecastPanelNativeGlobalState>();

    // Late-row guard: rows after the finalize claim would be silently dropped.
    gstate.barrier.AssertCollecting("_ts_forecast_panel_native");

    // Extract batch locally (no lock)
    struct TempRow {
        Value group_val;
        string group_key;
        int64_t date_micros;
        double value;
        bool valid;
    };
    vector<TempRow> batch;

    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull()) continue;

        TempRow row;
        row.group_val = group_val;
        row.group_key = GetGroupKey(group_val);

        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                row.date_micros = DateToMicroseconds(date_val.GetValue<date_t>());
                break;
            case DateColumnType::TIMESTAMP:
                row.date_micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
                break;
            case DateColumnType::INTEGER:
                row.date_micros = date_val.GetValue<int32_t>();
                break;
            case DateColumnType::BIGINT:
                row.date_micros = date_val.GetValue<int64_t>();
                break;
        }

        row.value = value_val.IsNull() ? 0.0 : value_val.GetValue<double>();
        row.valid = !value_val.IsNull();
        batch.push_back(std::move(row));
    }

    // Lock once, insert all
    {
        std::lock_guard<std::mutex> lock(gstate.groups_mutex);
        for (auto &row : batch) {
            if (gstate.groups.find(row.group_key) == gstate.groups.end()) {
                gstate.groups[row.group_key] = ForecastGroupData();
                gstate.groups[row.group_key].group_value = row.group_val;
                gstate.group_order.push_back(row.group_key);
            }
            auto &grp = gstate.groups[row.group_key];
            grp.dates.push_back(row.date_micros);
            grp.values.push_back(row.value);
            grp.validity.push_back(row.valid);
        }
    }

    SetOutputCardinality(output, 0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize — panel alignment + single global fit + emit rows
// ============================================================================

static OperatorFinalizeResultType TsForecastPanelNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsForecastPanelNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsForecastPanelNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsForecastPanelNativeLocalState>();

    // Last-finalizer claim (ts_collect_finalize.hpp): the local state whose
    // arrival completes the count of registered local states processes; every
    // other one finishes at once (no spin-wait).
    if (!lstate.owns_finalize) {
        if (lstate.arrived_at_final) {
            return OperatorFinalizeResultType::FINISHED;
        }
        lstate.arrived_at_final = true;
        if (!gstate.barrier.ArriveAtFinal()) {
            return OperatorFinalizeResultType::FINISHED;
        }
        lstate.owns_finalize = true;
    }

    // Panel processing (single thread)
    if (!gstate.processed) {
        // ------------------------------------------------------------------
        // 1. Sort each series by date; build shared date grid (union of dates)
        // ------------------------------------------------------------------

        std::set<int64_t> all_dates_set;

        // Per-group: sort by date and gather all dates
        for (const auto &group_key : gstate.group_order) {
            auto &grp = gstate.groups[group_key];
            if (grp.dates.empty()) continue;

            // Sort group by date
            vector<size_t> indices(grp.dates.size());
            for (size_t i = 0; i < indices.size(); i++) indices[i] = i;
            std::sort(indices.begin(), indices.end(),
                [&grp](size_t a, size_t b) { return grp.dates[a] < grp.dates[b]; });

            vector<int64_t> sorted_dates(grp.dates.size());
            vector<double>  sorted_values(grp.values.size());
            vector<bool>    sorted_validity(grp.validity.size());
            for (size_t i = 0; i < indices.size(); i++) {
                sorted_dates[i]    = grp.dates[indices[i]];
                sorted_values[i]   = grp.values[indices[i]];
                sorted_validity[i] = grp.validity[indices[i]];
            }
            grp.dates    = sorted_dates;
            grp.values   = sorted_values;
            grp.validity = sorted_validity;

            for (auto d : grp.dates) {
                all_dates_set.insert(d);
            }
        }

        if (all_dates_set.empty()) {
            gstate.processed = true;
            SetOutputCardinality(output, 0);
            return OperatorFinalizeResultType::FINISHED;
        }

        vector<int64_t> shared_grid(all_dates_set.begin(), all_dates_set.end());
        size_t grid_len = shared_grid.size();

        // Forecast dates computed via the Rust D-12 calendar rule
        // (StepFromFrequency + ComputeFutureDates, plan 12-05/12-06) instead
        // of this file's own month arithmetic + the day-of-month clamp this file used to replicate per-call.
        // Same (step_months, step_micros) pair used by both stepping sites
        // below — only the `observed` series (anchor date) differs.
        int64_t step_months = 0;
        int64_t step_micros = 0;
        StepFromFrequency(bind_data.frequency_type, bind_data.frequency_seconds, bind_data.frequency_is_raw,
                           bind_data.date_col_type, step_months, step_micros);

        // ------------------------------------------------------------------
        // 2. Align each series to shared_grid; apply drop rule (< 10 valid points)
        // ------------------------------------------------------------------

        // Minimum valid-observation threshold for panel participation
        static const size_t MIN_VALID_COUNT = 10;

        vector<vector<double>> aligned_series;  // [n_kept][grid_len]
        vector<string>         valid_keys;      // group keys of kept series

        for (const auto &group_key : gstate.group_order) {
            auto &grp = gstate.groups[group_key];
            if (grp.dates.empty()) {
                // Emit DROPPED rows and continue — date is NULL because no data was ingested
                for (int64_t h = 1; h <= bind_data.horizon; h++) {
                    PanelOutputRow row;
                    row.group_key = group_key;
                    row.group_value = grp.group_value;
                    row.forecast_step = h;
                    row.date = 0;
                    row.date_null = true;  // emit NULL; date=0 would produce epoch 1970-01-01
                    row.point_forecast = std::numeric_limits<double>::quiet_NaN();
                    row.model_name = "DROPPED: too_short";
                    gstate.results.push_back(row);
                }
                continue;
            }

            // Build date→value map (most-recent value wins on duplicate dates)
            std::map<int64_t, double> date_to_value;
            for (size_t i = 0; i < grp.dates.size(); i++) {
                if (grp.validity[i]) {
                    date_to_value[grp.dates[i]] = grp.values[i];
                }
            }

            // Count valid observations
            size_t valid_count = date_to_value.size();

            if (valid_count < MIN_VALID_COUNT) {
                // Drop: emit DROPPED rows for each horizon step. Forecast
                // dates are anchored on this series' own observed dates
                // (grp.dates — already sorted ascending, NULL dates were
                // skipped on ingestion so every entry is a real observation).
                vector<int64_t> future_ts =
                    ComputeFutureDates(grp.dates, static_cast<idx_t>(bind_data.horizon), step_months, step_micros);
                for (int64_t h = 1; h <= bind_data.horizon; h++) {
                    PanelOutputRow row;
                    row.group_key = group_key;
                    row.group_value = grp.group_value;
                    row.forecast_step = h;
                    row.date = future_ts[h - 1];
                    row.date_null = false;  // date was computed from last observed date
                    row.point_forecast = std::numeric_limits<double>::quiet_NaN();
                    row.model_name = "DROPPED: too_short";
                    gstate.results.push_back(row);
                }
                continue;
            }

            // Align series to shared_grid: present dates → value, absent → NaN
            vector<double> aligned(grid_len, std::numeric_limits<double>::quiet_NaN());
            for (size_t g = 0; g < grid_len; g++) {
                auto it = date_to_value.find(shared_grid[g]);
                if (it != date_to_value.end()) {
                    aligned[g] = it->second;
                }
            }

            aligned_series.push_back(std::move(aligned));
            valid_keys.push_back(group_key);
        }

        size_t n_kept = aligned_series.size();

        if (n_kept == 0) {
            // All series dropped — no forecasting to do. Fall through to the output loop
            // so any accumulated DROPPED sentinel rows in gstate.results are still emitted.
            gstate.processed = true;
            // (do not return here — fall through to the output-batching block below)
        } else if (n_kept < 3) {
            // Fewer than 3 usable series — we must still emit any accumulated DROPPED rows
            // first, then raise an error.  Throwing here would discard them; instead record
            // the message for a deferred throw once the output-batching block has flushed
            // all DROPPED rows (checked just before returning FINISHED below).
            gstate.processed = true;
            gstate.deferred_error_message = StringUtil::Format(
                "ts_forecast_panel_by: panel has fewer than 3 usable series after alignment "
                "(need >= 3 for cross-series global learning). "
                "Check series lengths (minimum %llu valid observations required).",
                (unsigned long long)MIN_VALID_COUNT);
            // Fall through to the output-batching block to emit any DROPPED rows first.
        } else {
        // ------------------------------------------------------------------
        // 3. Build flat matrix: double[n_kept * grid_len], row-major
        // ------------------------------------------------------------------

        if (n_kept > 0 && grid_len > std::numeric_limits<size_t>::max() / n_kept) {
            throw InvalidInputException(
                "ts_forecast_panel_by: panel too large (n_series=%zu x grid_len=%zu overflows size_t)",
                n_kept, grid_len);
        }
        vector<double> flat_matrix(n_kept * grid_len);
        for (size_t i = 0; i < n_kept; i++) {
            std::copy(aligned_series[i].begin(), aligned_series[i].end(),
                      flat_matrix.data() + i * grid_len);
        }

        // ------------------------------------------------------------------
        // 4. Single panel FFI call (fit-once-emit-many)
        // ------------------------------------------------------------------

        PanelForecastResult panel_result;
        memset(&panel_result, 0, sizeof(panel_result));
        AnofoxError error;

        bool ok = anofox_ts_forecast_panel(
            flat_matrix.data(),
            n_kept,
            grid_len,
            bind_data.method.c_str(),
            static_cast<size_t>(bind_data.horizon),
            static_cast<size_t>(bind_data.seasonal_period),
            bind_data.croston_variant.empty() ? nullptr : bind_data.croston_variant.c_str(),
            bind_data.model_pool.empty()      ? nullptr : bind_data.model_pool.c_str(),
            &panel_result,
            &error
        );

        if (!ok) {
            throw InvalidInputException(
                "ts_forecast_panel_by (method='%s'): %s",
                bind_data.method, string(error.message));
        }

        string model_name_str(panel_result.model_name);
        size_t n_horizon = panel_result.n_horizon;

        // ------------------------------------------------------------------
        // 5. Emit output rows: n_kept × horizon rows
        // ------------------------------------------------------------------

        // Shared future grid anchored at shared_grid.back() (same base the
        // old code used) — computed once via the Rust D-12 calendar rule and
        // reused for every kept series below.
        vector<int64_t> future_ts = ComputeFutureDates(shared_grid, n_horizon, step_months, step_micros);

        for (size_t s = 0; s < n_kept; s++) {
            const auto &group_key = valid_keys[s];
            auto &grp = gstate.groups[group_key];

            for (size_t h = 0; h < n_horizon; h++) {
                PanelOutputRow row;
                row.group_key   = group_key;
                row.group_value = grp.group_value;
                row.forecast_step = static_cast<int64_t>(h + 1);
                row.date = future_ts[h];
                row.date_null = false;
                row.point_forecast = panel_result.forecasts[s * n_horizon + h];
                row.model_name     = model_name_str;
                gstate.results.push_back(row);
            }
        }

        // Free Rust-allocated forecast buffer — do NOT access panel_result.forecasts after this
        anofox_free_panel_forecast_result(&panel_result);

        gstate.processed = true;
        } // end else (n_kept >= 3) block
    }

    // ------------------------------------------------------------------
    // Output results in STANDARD_VECTOR_SIZE batches (mirrors ts_forecast_native.cpp)
    // ------------------------------------------------------------------

    idx_t remaining = gstate.results.size() - gstate.output_offset;
    if (remaining == 0) {
        // All rows (including any DROPPED sentinels) have been emitted.
        // If n_kept<3 deferred an error, raise it now so the caller gets both
        // the DROPPED diagnostics and a clear exception.
        if (!gstate.deferred_error_message.empty()) {
            throw InvalidInputException("%s", gstate.deferred_error_message);
        }
        SetOutputCardinality(output, 0);
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t to_output = std::min(remaining, static_cast<idx_t>(STANDARD_VECTOR_SIZE));
    SetOutputCardinality(output, to_output);

    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    for (idx_t i = 0; i < to_output; i++) {
        auto &row = gstate.results[gstate.output_offset + i];

        // col 0: group value
        output.data[0].SetValue(i, row.group_value);

        // col 1: forecast_step (INTEGER)
        output.data[1].SetValue(i, Value::INTEGER(static_cast<int32_t>(row.forecast_step)));

        // col 2: date (type-preserving; NULL when no data was ingested for this series)
        if (row.date_null) {
            output.data[2].SetValue(i, Value(bind_data.date_logical_type));
        } else {
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
        }

        // col 3: yhat (DOUBLE)
        output.data[3].SetValue(i, Value::DOUBLE(row.point_forecast));

        // col 4: model_name (VARCHAR)
        output.data[4].SetValue(i, Value(row.model_name));
    }

    gstate.output_offset += to_output;

    if (gstate.output_offset >= gstate.results.size()) {
        // All rows flushed; raise any deferred error from the n_kept<3 path.
        if (!gstate.deferred_error_message.empty()) {
            throw InvalidInputException("%s", gstate.deferred_error_message);
        }
        return OperatorFinalizeResultType::FINISHED;
    }
    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsForecastPanelNativeFunction(ExtensionLoader &loader) {
    // Internal table-in-out function: (TABLE, horizon, frequency, method, params)
    // Input table must have 3 columns: group_col, date_col, value_col
    // Note: This is an internal function (prefixed with _) called by ts_forecast_panel_by macro
    TableFunction func("_ts_forecast_panel_native",
        {LogicalType::TABLE, LogicalType::INTEGER, LogicalType::VARCHAR,
         LogicalType::VARCHAR, LogicalType::ANY},
        nullptr,  // No execute function — use in_out_function
        TsForecastPanelNativeBind,
        TsForecastPanelNativeInitGlobal,
        TsForecastPanelNativeInitLocal);

    func.in_out_function       = TsForecastPanelNativeInOut;
    func.in_out_function_final = TsForecastPanelNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
