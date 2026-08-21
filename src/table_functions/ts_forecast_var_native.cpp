#include "ts_forecast_var_native.hpp"
#include "ts_fill_gaps_native.hpp"  // ParseFrequencyWithType, date helpers, DateColumnType
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <cmath>
#include <cstring>
#include <limits>

namespace duckdb {

// ============================================================================
// _ts_forecast_var_native — Internal table function for VAR multivariate forecasting.
//
// Collects all rows from a table with K value columns in-memory, then makes ONE
// anofox_ts_forecast_var call across all K variables simultaneously (single-panel
// fit). Output is LONG format: one row per (variable, horizon step).
//
// v1: single-panel only (no group_col). Use ts_forecast_var_by macro instead.
//
// Users should call ts_forecast_var_by() instead of this function directly.
// ============================================================================

// ============================================================================
// Bind Data
// ============================================================================

struct TsForecastVarNativeBindData : public TableFunctionData {
    int64_t horizon = 7;
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;
    FrequencyType frequency_type = FrequencyType::FIXED;
    int64_t order = 1;

    // Type preservation for date column
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);

    // Value column information (resolved in Bind from input.input_table_names)
    vector<string> value_col_names;    // column names from the value_cols VARCHAR[] arg
    vector<idx_t>  value_col_indices;  // column indices in the input table schema
};

// ============================================================================
// Output Row Structure
// ============================================================================

struct VarOutputRow {
    string variable;       // variable name (from value_col_names)
    int64_t forecast_step; // 1-based step
    int64_t date;          // microseconds (or raw int for INTEGER/BIGINT date cols)
    bool date_null;
    double forecast_value;
};

// ============================================================================
// Local State — per-thread flags only
// ============================================================================

struct TsForecastVarNativeLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool registered_collector = false;
    bool registered_finalizer = false;
};

// ============================================================================
// Global State — thread-safe collection + single-thread finalize
// ============================================================================

struct TsForecastVarNativeGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override { return 999999; }

    std::mutex data_mutex;
    vector<int64_t> dates;                    // date column (micros)
    vector<vector<double>> series_data;       // [k_vars][n_obs] — NaN for null
    vector<vector<bool>>   series_valid;      // [k_vars][n_obs] — true = non-null
    vector<VarOutputRow> results;
    bool processed = false;
    idx_t output_offset = 0;

    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};
};

// ============================================================================
// Param Helpers (mirrors ts_forecast_panel_native.cpp)
// ============================================================================

static int64_t ParseInt64FromVarParams(const Value &params_value, const string &key, int64_t default_val) {
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
            if (child_types[i].first == key && !struct_children[i].IsNull()) {
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

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsForecastVarNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsForecastVarNativeBindData>();

    // Positional args: TABLE, horizon (INT), frequency (VARCHAR), order (INT), value_cols (LIST(VARCHAR)), params (ANY)
    // Parse horizon (index 1)
    if (input.inputs.size() >= 2) {
        bind_data->horizon = input.inputs[1].GetValue<int64_t>();
        if (bind_data->horizon <= 0) {
            throw InvalidInputException("ts_forecast_var_by: horizon must be > 0, got %lld", (long long)bind_data->horizon);
        }
    }

    // Parse frequency (index 2)
    if (input.inputs.size() >= 3) {
        string freq_str = input.inputs[2].GetValue<string>();
        auto parsed = ParseFrequencyWithType(freq_str);
        bind_data->frequency_seconds = parsed.seconds;
        bind_data->frequency_is_raw = parsed.is_raw;
        bind_data->frequency_type = parsed.type;
    }

    // Parse order (index 3) — lag order p; default 1
    if (input.inputs.size() >= 4 && !input.inputs[3].IsNull()) {
        bind_data->order = input.inputs[3].GetValue<int64_t>();
        if (bind_data->order <= 0) {
            bind_data->order = 1;
        }
    }

    // Parse value_cols (index 4) — VARCHAR[] literal naming the K value columns
    if (input.inputs.size() >= 5 && !input.inputs[4].IsNull()) {
        auto &cols_list = ListValue::GetChildren(input.inputs[4]);
        if (cols_list.empty()) {
            throw InvalidInputException("ts_forecast_var_by: value_cols must contain at least one column name");
        }
        for (auto &col_val : cols_list) {
            string col_name = col_val.GetValue<string>();
            bind_data->value_col_names.push_back(col_name);
        }
    }

    if (bind_data->value_col_names.empty()) {
        throw InvalidInputException("ts_forecast_var_by: value_cols argument is required and must be non-empty");
    }

    // Resolve value column names → indices in the input table schema.
    // The input table schema: col 0 = date_col, then all other cols (from the * in the subselect).
    // The first column (index 0) is always the date column.
    for (const auto &col_name : bind_data->value_col_names) {
        bool found = false;
        for (idx_t i = 0; i < input.input_table_names.size(); i++) {
            if (input.input_table_names[i] == col_name) {
                bind_data->value_col_indices.push_back(i);
                found = true;
                break;
            }
        }
        if (!found) {
            throw InvalidInputException(
                "ts_forecast_var_by: column '%s' not found in input table. "
                "Available columns: %s",
                col_name.c_str(),
                StringUtil::Join(input.input_table_names, ", ").c_str());
        }
    }

    // Detect date column type from input table (always index 0 from the subselect)
    if (input.input_table_types.empty()) {
        throw InvalidInputException("ts_forecast_var_by: input table has no columns");
    }
    bind_data->date_logical_type = input.input_table_types[0];
    switch (input.input_table_types[0].id()) {
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
                "ts_forecast_var_by: date column (first column in subselect) must be "
                "DATE, TIMESTAMP, INTEGER, or BIGINT, got: %s",
                input.input_table_types[0].ToString().c_str());
    }

    // Output schema: variable VARCHAR, forecast_step BIGINT, <date_col_name> <date_type>, forecast_value DOUBLE
    string date_col_name = input.input_table_names.size() > 0 ? input.input_table_names[0] : "date";

    names.push_back("variable");
    return_types.push_back(LogicalType::VARCHAR);

    names.push_back("forecast_step");
    return_types.push_back(LogicalType::BIGINT);

    names.push_back(date_col_name);
    return_types.push_back(bind_data->date_logical_type);

    names.push_back("forecast_value");
    return_types.push_back(LogicalType::DOUBLE);

    return bind_data;
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsForecastVarNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    auto gstate = make_uniq<TsForecastVarNativeGlobalState>();
    // Initialise per-variable data containers
    auto &bind_data = input.bind_data->Cast<TsForecastVarNativeBindData>();
    size_t k = bind_data.value_col_names.size();
    gstate->series_data.resize(k);
    gstate->series_valid.resize(k);
    return gstate;
}

static unique_ptr<LocalTableFunctionState> TsForecastVarNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsForecastVarNativeLocalState>();
}

// ============================================================================
// In-Out Function — buffers incoming rows
// ============================================================================

static OperatorResultType TsForecastVarNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsForecastVarNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsForecastVarNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsForecastVarNativeLocalState>();

    // Register this thread as a collector (first call only)
    if (!lstate.registered_collector) {
        gstate.threads_collecting.fetch_add(1);
        lstate.registered_collector = true;
    }

    size_t k_vars = bind_data.value_col_names.size();

    // Extract batch locally (no lock)
    struct TempRow {
        int64_t date_micros;
        vector<double> values;  // k_vars values (NaN for null)
        vector<bool>   valid;   // k_vars validity flags
    };
    vector<TempRow> batch;

    for (idx_t i = 0; i < input.size(); i++) {
        // Column 0 is always the date column
        Value date_val = input.data[0].GetValue(i);
        if (date_val.IsNull()) continue;

        TempRow row;
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

        row.values.resize(k_vars, std::numeric_limits<double>::quiet_NaN());
        row.valid.resize(k_vars, false);

        for (idx_t v = 0; v < k_vars; v++) {
            idx_t col_idx = bind_data.value_col_indices[v];
            Value val = input.data[col_idx].GetValue(i);
            if (!val.IsNull()) {
                row.values[v] = val.GetValue<double>();
                row.valid[v] = true;
            }
            // else leave as NaN / false
        }

        batch.push_back(std::move(row));
    }

    // Lock once, insert all
    {
        std::lock_guard<std::mutex> lock(gstate.data_mutex);
        for (auto &row : batch) {
            gstate.dates.push_back(row.date_micros);
            for (idx_t v = 0; v < k_vars; v++) {
                gstate.series_data[v].push_back(row.values[v]);
                gstate.series_valid[v].push_back(row.valid[v]);
            }
        }
    }

    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize — single global VAR fit + emit long-format rows
// ============================================================================

static OperatorFinalizeResultType TsForecastVarNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsForecastVarNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsForecastVarNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsForecastVarNativeLocalState>();

    // Barrier + CAS claim (mirrors ts_forecast_panel_native.cpp)
    if (!lstate.registered_finalizer) {
        if (lstate.registered_collector) {
            gstate.threads_done_collecting.fetch_add(1);
        }
        lstate.registered_finalizer = true;
    }
    if (!lstate.owns_finalize) {
        bool expected = false;
        if (!gstate.finalize_claimed.compare_exchange_strong(expected, true)) {
            return OperatorFinalizeResultType::FINISHED;
        }
        lstate.owns_finalize = true;
        while (gstate.threads_done_collecting.load() < gstate.threads_collecting.load()) {
            std::this_thread::yield();
        }
    }

    // Single-thread processing
    if (!gstate.processed) {
        size_t k_vars = bind_data.value_col_names.size();
        size_t n_obs = gstate.dates.size();

        if (n_obs == 0) {
            gstate.processed = true;
            output.SetCardinality(0);
            return OperatorFinalizeResultType::FINISHED;
        }

        // ------------------------------------------------------------------
        // 1. Sort by date
        // ------------------------------------------------------------------
        vector<size_t> indices(n_obs);
        for (size_t i = 0; i < n_obs; i++) indices[i] = i;
        std::sort(indices.begin(), indices.end(),
            [&gstate](size_t a, size_t b) { return gstate.dates[a] < gstate.dates[b]; });

        vector<int64_t> sorted_dates(n_obs);
        vector<vector<double>> sorted_data(k_vars, vector<double>(n_obs));
        for (size_t i = 0; i < n_obs; i++) {
            sorted_dates[i] = gstate.dates[indices[i]];
            for (size_t v = 0; v < k_vars; v++) {
                sorted_data[v][i] = gstate.series_data[v][indices[i]];
                // Preserve NaN for missing values — the Rust FFI imputes them
            }
        }

        int64_t last_obs_date = sorted_dates.back();

        // ------------------------------------------------------------------
        // 2. Verify equal-length alignment (all columns have same n_obs after collect)
        //    and check for under-determination
        // ------------------------------------------------------------------
        // Count valid (non-NaN) per column to check alignment
        vector<size_t> valid_counts(k_vars, 0);
        for (size_t v = 0; v < k_vars; v++) {
            for (size_t t = 0; t < n_obs; t++) {
                if (!std::isnan(sorted_data[v][t])) {
                    valid_counts[v]++;
                }
            }
        }
        // Check for equal effective lengths (Pitfall 4 from RESEARCH.md)
        size_t min_valid = valid_counts[0];
        size_t max_valid = valid_counts[0];
        for (size_t v = 1; v < k_vars; v++) {
            min_valid = std::min(min_valid, valid_counts[v]);
            max_valid = std::max(max_valid, valid_counts[v]);
        }
        if (min_valid != max_valid) {
            throw InvalidInputException(
                "ts_forecast_var_by: VAR requires all value columns to have the same number "
                "of valid (non-null) observations. Found %zu to %zu valid observations across columns. "
                "Ensure all value columns cover the same date range without differing null patterns.",
                min_valid, max_valid);
        }

        // Soft under-determination check (Pitfall 5): n_eff < k*order+1 → error
        size_t n_eff = min_valid;
        size_t order = static_cast<size_t>(bind_data.order);
        size_t min_required = k_vars * order + 1;
        if (n_eff < min_required) {
            throw InvalidInputException(
                "ts_forecast_var_by: insufficient observations for VAR(%zu) with %zu variables. "
                "Need at least k*p+1=%zu valid observations, got %zu. "
                "Reduce order or provide more data.",
                order, k_vars, min_required, n_eff);
        }

        // ------------------------------------------------------------------
        // 3. Build flat variable-major matrix: double[k_vars * n_obs]
        //    NaN values are passed to the Rust FFI, which imputes them internally.
        // ------------------------------------------------------------------
        vector<double> flat;
        flat.reserve(k_vars * n_obs);
        for (size_t v = 0; v < k_vars; v++) {
            for (size_t t = 0; t < n_obs; t++) {
                flat.push_back(sorted_data[v][t]);
            }
        }

        // ------------------------------------------------------------------
        // 4. Call anofox_ts_forecast_var (fit-once-emit-many for all K variables)
        // ------------------------------------------------------------------
        VARForecastResult var_result;
        memset(&var_result, 0, sizeof(var_result));
        AnofoxError error;

        bool ok = anofox_ts_forecast_var(
            flat.data(),
            k_vars,
            n_obs,
            static_cast<size_t>(bind_data.order),
            static_cast<size_t>(bind_data.horizon),
            &var_result,
            &error
        );

        if (!ok) {
            throw InvalidInputException(
                "ts_forecast_var_by (order=%lld): %s",
                (long long)bind_data.order, string(error.message).c_str());
        }

        // ------------------------------------------------------------------
        // 5. Emit long-format output rows: k_vars × horizon rows total
        //    var_result.forecasts[v * horizon + h] = forecast for variable v at step h (0-based)
        // ------------------------------------------------------------------
        size_t horizon = static_cast<size_t>(bind_data.horizon);

        for (size_t v = 0; v < k_vars; v++) {
            for (size_t h = 0; h < horizon; h++) {
                VarOutputRow row;
                row.variable = bind_data.value_col_names[v];
                row.forecast_step = static_cast<int64_t>(h + 1);

                // Calendar-aware forecast date arithmetic anchored at last observation date
                int64_t steps = static_cast<int64_t>(h + 1);
                if (bind_data.frequency_type == FrequencyType::MONTHLY ||
                    bind_data.frequency_type == FrequencyType::QUARTERLY ||
                    bind_data.frequency_type == FrequencyType::YEARLY) {
                    date_t base_date = MicrosecondsToDate(last_obs_date);
                    int32_t year, month, day;
                    Date::Convert(base_date, year, month, day);
                    int64_t months_to_add = steps * bind_data.frequency_seconds;
                    if (bind_data.frequency_type == FrequencyType::QUARTERLY) months_to_add *= 3;
                    else if (bind_data.frequency_type == FrequencyType::YEARLY) months_to_add *= 12;
                    int64_t total_months = static_cast<int64_t>(year) * 12 + (month - 1) + months_to_add;
                    int32_t new_year = static_cast<int32_t>(total_months / 12);
                    int32_t new_month = static_cast<int32_t>((total_months % 12) + 1);
                    if (new_month < 1) { new_month += 12; new_year -= 1; }
                    int32_t max_day = Date::MonthDays(new_year, new_month);
                    int32_t new_day = std::min(day, max_day);
                    row.date = DateToMicroseconds(Date::FromDate(new_year, new_month, new_day));
                } else {
                    int64_t freq_micros;
                    if (bind_data.date_col_type == DateColumnType::INTEGER ||
                        bind_data.date_col_type == DateColumnType::BIGINT) {
                        freq_micros = bind_data.frequency_seconds;
                    } else {
                        freq_micros = bind_data.frequency_is_raw
                            ? bind_data.frequency_seconds * 86400LL * 1000000LL
                            : bind_data.frequency_seconds * 1000000LL;
                    }
                    row.date = last_obs_date + freq_micros * steps;
                }
                row.date_null = false;
                row.forecast_value = var_result.forecasts[v * horizon + h];
                gstate.results.push_back(std::move(row));
            }
        }

        // Free Rust-allocated forecast buffer — do NOT access var_result.forecasts after this
        anofox_free_var_forecast_result(&var_result);

        gstate.processed = true;
    }

    // ------------------------------------------------------------------
    // Output results in STANDARD_VECTOR_SIZE batches
    // ------------------------------------------------------------------

    idx_t remaining = static_cast<idx_t>(gstate.results.size()) - gstate.output_offset;
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

        // col 0: variable (VARCHAR)
        output.data[0].SetValue(i, Value(row.variable));

        // col 1: forecast_step (BIGINT)
        output.data[1].SetValue(i, Value::BIGINT(row.forecast_step));

        // col 2: date (type-preserving)
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

        // col 3: forecast_value (DOUBLE)
        output.data[3].SetValue(i, Value::DOUBLE(row.forecast_value));
    }

    gstate.output_offset += to_output;

    if (gstate.output_offset >= static_cast<idx_t>(gstate.results.size())) {
        return OperatorFinalizeResultType::FINISHED;
    }
    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsForecastVarNativeFunction(ExtensionLoader &loader) {
    // Internal table-in-out function: (TABLE, horizon, frequency, order, value_cols, params)
    // Input table must have: date_col first, then all columns (via subselect in macro)
    // Note: This is an internal function (prefixed with _) called by ts_forecast_var_by macro.
    //
    // v1: single-panel only — no group_col. One VAR fit over the entire input table.
    TableFunction func("_ts_forecast_var_native",
        {LogicalType::TABLE, LogicalType::INTEGER, LogicalType::VARCHAR,
         LogicalType::INTEGER, LogicalType::LIST(LogicalType::VARCHAR), LogicalType::ANY},
        nullptr,  // No execute function — use in_out_function
        TsForecastVarNativeBind,
        TsForecastVarNativeInitGlobal,
        TsForecastVarNativeInitLocal);

    func.in_out_function       = TsForecastVarNativeInOut;
    func.in_out_function_final = TsForecastVarNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
