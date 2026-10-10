#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "ts_fill_gaps_native.hpp"
#include "ts_forecast_params.hpp"
#include "ts_future_dates.hpp"
#include "ts_list_marshal.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/planner/expression/bound_function_expression.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/interval.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/vector.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace duckdb {

// ============================================================================
// _ts_forecast_scalar — scalar function for parallel GROUP BY forecasting
//
// Signature:
//   _ts_forecast_scalar(dates LIST, values LIST, horizon INT, frequency VARCHAR,
//                       method VARCHAR, params MAP) -> LIST(STRUCT(...))
//
// Called per-group by the ts_forecast_by macro via:
//   SELECT group_col, unnest(_ts_forecast_scalar(...), recursive := true)
//   FROM source GROUP BY group_col
//
// DuckDB parallelizes the GROUP BY across cores natively.
// ============================================================================

// ============================================================================
// Bind Data
// ============================================================================

struct TsForecastScalarBindData : public FunctionData {
    int64_t horizon = 7;
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;
    FrequencyType frequency_type = FrequencyType::FIXED;

    string method = "AutoETS";
    string model_spec = "";
    int64_t seasonal_period = 0;
    double confidence_level = 0.90;
    int64_t window = 0;
    string seasonal_periods_str = "";
    string model_pool = "";
    string laplace_variant = "";
    bool laplace_seasonal_batch_init = false;
    int64_t garch_p = 0;
    int64_t garch_q = 0;
    string kalman_model = "";
    // AutoEnsemble params (Phase 4)
    int64_t ensemble_top_k = 0;
    string ensemble_method = "";
    // ARIMA order params (D-08/D-09, plan 10-02)
    string arima_order = "";
    string seasonal_order = "";

    DateColumnType date_col_type = DateColumnType::DATE;

    unique_ptr<FunctionData> Copy() const override {
        auto copy = make_uniq<TsForecastScalarBindData>();
        copy->horizon = horizon;
        copy->frequency_seconds = frequency_seconds;
        copy->frequency_is_raw = frequency_is_raw;
        copy->frequency_type = frequency_type;
        copy->method = method;
        copy->model_spec = model_spec;
        copy->seasonal_period = seasonal_period;
        copy->confidence_level = confidence_level;
        copy->window = window;
        copy->seasonal_periods_str = seasonal_periods_str;
        copy->model_pool = model_pool;
        copy->laplace_variant = laplace_variant;
        copy->laplace_seasonal_batch_init = laplace_seasonal_batch_init;
        copy->garch_p = garch_p;
        copy->garch_q = garch_q;
        copy->kalman_model = kalman_model;
        copy->ensemble_top_k = ensemble_top_k;
        copy->ensemble_method = ensemble_method;
        copy->arima_order = arima_order;
        copy->seasonal_order = seasonal_order;
        copy->date_col_type = date_col_type;
        return std::move(copy);
    }

    bool Equals(const FunctionData &other_p) const override {
        auto &other = other_p.Cast<TsForecastScalarBindData>();
        return horizon == other.horizon && frequency_seconds == other.frequency_seconds &&
               method == other.method && seasonal_period == other.seasonal_period &&
               confidence_level == other.confidence_level;
    }
};

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsForecastScalarBind(
    ClientContext &context,
    ScalarFunction &bound_function,
    vector<unique_ptr<Expression>> &arguments) {

    auto bind_data = make_uniq<TsForecastScalarBindData>();

    // Detect date column type from LIST child type (argument 0 = dates)
    auto &date_list_type = arguments[0]->return_type;
    if (date_list_type.id() == LogicalTypeId::LIST) {
        auto &child_type = ListType::GetChildType(date_list_type);
        switch (child_type.id()) {
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
                    "Date list must contain DATE, TIMESTAMP, INTEGER, or BIGINT, got: %s",
                    child_type.ToString().c_str());
        }
    }

    // Build the output STRUCT type for list elements
    // Output: LIST(STRUCT(forecast_step INT, ds <date_type>, yhat DOUBLE, yhat_lower DOUBLE, yhat_upper DOUBLE, model_name VARCHAR))
    auto &date_child_type = ListType::GetChildType(date_list_type);
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("forecast_step", LogicalType::INTEGER));
    struct_children.push_back(make_pair("ds", date_child_type));
    struct_children.push_back(make_pair("yhat", LogicalType::DOUBLE));
    struct_children.push_back(make_pair("yhat_lower", LogicalType::DOUBLE));
    struct_children.push_back(make_pair("yhat_upper", LogicalType::DOUBLE));
    struct_children.push_back(make_pair("model_name", LogicalType::VARCHAR));

    bound_function.return_type = LogicalType::LIST(LogicalType::STRUCT(std::move(struct_children)));

    return std::move(bind_data);
}

// ============================================================================
// Date Conversion Helpers
// ============================================================================

static int64_t ExtractDateMicros(const Value &date_val, DateColumnType date_col_type) {
    switch (date_col_type) {
        case DateColumnType::DATE:
            return DateToMicroseconds(date_val.GetValue<date_t>());
        case DateColumnType::TIMESTAMP:
            return TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
        case DateColumnType::INTEGER:
            return static_cast<int64_t>(date_val.GetValue<int32_t>());
        case DateColumnType::BIGINT:
            return date_val.GetValue<int64_t>();
        default:
            return 0;
    }
}

static Value MicrosToDateValue(int64_t micros, DateColumnType date_col_type) {
    switch (date_col_type) {
        case DateColumnType::DATE:
            return Value::DATE(MicrosecondsToDate(micros));
        case DateColumnType::TIMESTAMP:
            return Value::TIMESTAMP(MicrosecondsToTimestamp(micros));
        case DateColumnType::INTEGER:
            return Value::INTEGER(static_cast<int32_t>(micros));
        case DateColumnType::BIGINT:
            return Value::BIGINT(micros);
        default:
            return Value::BIGINT(micros);
    }
}

// ============================================================================
// Execute Function
// ============================================================================
//
// Forecast dates are computed via the Rust D-12 calendar rule
// (crates/anofox-fcst-core/src/calendar.rs future_dates, reached through
// ts_future_dates.hpp's StepFromFrequency + ComputeFutureDates) instead of
// this file's own month arithmetic — the month-end-preserving /
// day-of-month-clamp rule now matches ts_forecast_agg exactly (D-11
// parity), including on month-end series ending in a short month.

static void TsForecastScalarExecute(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &bind_data = state.expr.Cast<BoundFunctionExpression>().bind_info->Cast<TsForecastScalarBindData>();
    idx_t count = args.size();

    auto &date_list_vec = args.data[0];   // LIST(date)
    auto &value_list_vec = args.data[1];  // LIST(DOUBLE)
    auto &horizon_vec = args.data[2];     // INTEGER
    auto &freq_vec = args.data[3];        // VARCHAR
    auto &method_vec = args.data[4];      // VARCHAR
    auto &params_vec = args.data[5];      // MAP

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Unified formats for all inputs
    UnifiedVectorFormat date_list_data, value_list_data, horizon_data, freq_data, method_data, params_data;
    date_list_vec.ToUnifiedFormat(count, date_list_data);
    value_list_vec.ToUnifiedFormat(count, value_list_data);
    horizon_vec.ToUnifiedFormat(count, horizon_data);
    freq_vec.ToUnifiedFormat(count, freq_data);
    method_vec.ToUnifiedFormat(count, method_data);
    params_vec.ToUnifiedFormat(count, params_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto date_idx = date_list_data.sel->get_index(row_idx);
        auto value_idx = value_list_data.sel->get_index(row_idx);

        if (!date_list_data.validity.RowIsValid(date_idx) ||
            !value_list_data.validity.RowIsValid(value_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // --- Extract values from LIST(DOUBLE) ---
        auto value_entries = UnifiedVectorFormat::GetData<list_entry_t>(value_list_data);
        auto &value_entry = value_entries[value_idx];
        auto &value_child = ListVector::GetEntry(value_list_vec);

        UnifiedVectorFormat value_child_data;
        value_child.ToUnifiedFormat(ListVector::GetListSize(value_list_vec), value_child_data);
        auto value_values = UnifiedVectorFormat::GetData<double>(value_child_data);

        idx_t n = value_entry.length;
        idx_t offset = value_entry.offset;

        if (n == 0) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // --- Extract dates from LIST(date) ---
        auto date_entries = UnifiedVectorFormat::GetData<list_entry_t>(date_list_data);
        auto &date_entry = date_entries[date_idx];
        auto &date_child = ListVector::GetEntry(date_list_vec);

        // Build sorted index by date. date_is_null tracks which entries had
        // a NULL date (date_micros[i]=0 is a placeholder, not a real
        // 1970-01-01 observation) so the calendar `observed` series below
        // (used for frequency/month-end detection) never includes a NULL
        // date disguised as epoch zero.
        vector<int64_t> date_micros(n);
        vector<bool> date_is_null(n);
        for (idx_t i = 0; i < n; i++) {
            Value dv = date_child.GetValue(date_entry.offset + i);
            if (dv.IsNull()) {
                date_micros[i] = 0;
                date_is_null[i] = true;
            } else {
                date_micros[i] = ExtractDateMicros(dv, bind_data.date_col_type);
                date_is_null[i] = false;
            }
        }

        vector<size_t> indices(n);
        for (size_t i = 0; i < n; i++) indices[i] = i;
        std::sort(indices.begin(), indices.end(),
            [&date_micros](size_t a, size_t b) { return date_micros[a] < date_micros[b]; });

        // Build sorted values + validity, and the NON-NULL-date `observed`
        // series (D-12) in the same ascending order — a subsequence of the
        // already-sorted `indices`, so it stays ascending without a second sort.
        vector<double> sorted_values(n);
        size_t validity_words = (n + 63) / 64;
        vector<uint64_t> validity(validity_words, 0);
        vector<int64_t> observed;
        observed.reserve(n);

        for (size_t i = 0; i < n; i++) {
            idx_t src = indices[i];
            idx_t child_idx = offset + src;
            auto unified_idx = value_child_data.sel->get_index(child_idx);

            if (value_child_data.validity.RowIsValid(unified_idx)) {
                sorted_values[i] = value_values[unified_idx];
                validity[i / 64] |= (1ULL << (i % 64));
            } else {
                sorted_values[i] = 0.0;
            }
            if (!date_is_null[src]) {
                observed.push_back(date_micros[src]);
            }
        }

        if (observed.empty()) {
            throw InvalidInputException("ts_forecast_by: all date values are NULL for this group");
        }

        // --- Parse per-row parameters ---
        auto h_idx = horizon_data.sel->get_index(row_idx);
        int32_t horizon = bind_data.horizon;
        if (horizon_data.validity.RowIsValid(h_idx)) {
            horizon = UnifiedVectorFormat::GetData<int32_t>(horizon_data)[h_idx];
        }

        auto m_idx = method_data.sel->get_index(row_idx);
        string method = bind_data.method;
        if (method_data.validity.RowIsValid(m_idx)) {
            method = UnifiedVectorFormat::GetData<string_t>(method_data)[m_idx].GetString();
        }

        auto f_idx = freq_data.sel->get_index(row_idx);
        auto freq_parsed = bind_data.frequency_type;
        auto freq_seconds = bind_data.frequency_seconds;
        auto freq_is_raw = bind_data.frequency_is_raw;
        if (freq_data.validity.RowIsValid(f_idx)) {
            string freq_str = UnifiedVectorFormat::GetData<string_t>(freq_data)[f_idx].GetString();
            auto parsed = ParseFrequencyWithType(freq_str);
            freq_seconds = parsed.seconds;
            freq_is_raw = parsed.is_raw;
            freq_parsed = parsed.type;
        }

        // Parse MAP params via the shared header (ts_forecast_params.hpp) —
        // the exact helper ts_forecast_agg.cpp now also uses, so this scalar
        // (ts_forecast_by's path) and the aggregate cannot drift (10-10,
        // FCST-06). A missing/NULL per-row params value falls back to a
        // fresh ForecastParamSet(), which carries the same defaults
        // TsForecastScalarBindData's unused-by-bind fields previously did.
        ForecastParamSet p;
        auto p_idx = params_data.sel->get_index(row_idx);
        if (params_data.validity.RowIsValid(p_idx)) {
            p = ParseForecastParamSet(params_vec.GetValue(row_idx));
        }

        // --- Build ForecastOptions ---
        ForecastOptions opts;
        memset(&opts, 0, sizeof(opts));
        ApplyForecastParamSet(p, method, opts);
        opts.horizon = static_cast<int>(horizon);
        opts.include_fitted = false;
        opts.include_residuals = false;

        // --- Call Rust FFI ---
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
            if (error.code == INVALID_MODEL || error.code == INVALID_INPUT) {
                throw InvalidInputException(string(error.message));
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // --- Build LIST(STRUCT(...)) result ---
        // Map this row's per-row frequency to a (step_months, step_micros)
        // pair and compute every forecast date in one Rust call (D-12,
        // D-11 parity with ts_forecast_agg) instead of the old per-step
        // ComputeForecastDate month arithmetic.
        int64_t step_months = 0;
        int64_t step_micros = 0;
        StepFromFrequency(freq_parsed, freq_seconds, freq_is_raw, bind_data.date_col_type, step_months,
                           step_micros);
        vector<int64_t> future_ts = ComputeFutureDates(observed, fcst_result.n_forecasts, step_months, step_micros);

        vector<Value> forecast_structs;
        forecast_structs.reserve(fcst_result.n_forecasts);

        for (size_t i = 0; i < fcst_result.n_forecasts; i++) {
            int64_t step = static_cast<int64_t>(i + 1);
            int64_t forecast_date = future_ts[i];

            child_list_t<Value> struct_values;
            struct_values.push_back(make_pair("forecast_step", Value::INTEGER(static_cast<int32_t>(step))));
            struct_values.push_back(make_pair("ds", MicrosToDateValue(forecast_date, bind_data.date_col_type)));
            struct_values.push_back(make_pair("yhat", Value::DOUBLE(fcst_result.point_forecasts[i])));
            // lower_bounds / upper_bounds are null when the model does not provide prediction
            // intervals (e.g. GARCH, Kalman in v1). Emit SQL NULL rather than crashing.
            struct_values.push_back(make_pair("yhat_lower",
                (fcst_result.lower_bounds != nullptr)
                    ? Value::DOUBLE(fcst_result.lower_bounds[i])
                    : Value(LogicalType::DOUBLE)));
            struct_values.push_back(make_pair("yhat_upper",
                (fcst_result.upper_bounds != nullptr)
                    ? Value::DOUBLE(fcst_result.upper_bounds[i])
                    : Value(LogicalType::DOUBLE)));
            struct_values.push_back(make_pair("model_name", Value(string(fcst_result.model_name))));

            forecast_structs.push_back(Value::STRUCT(std::move(struct_values)));
        }

        anofox_free_forecast_result(&fcst_result);

        // Set the LIST value
        result.SetValue(row_idx, Value::LIST(std::move(forecast_structs)));
    }
}

// ============================================================================
// _ts_future_dates — Rust D-12 calendar rule exposed directly to SQL macros
//
// Signature: _ts_future_dates(dates LIST(TIMESTAMP), horizon INTEGER, step
// INTERVAL) -> LIST(TIMESTAMP)
//
// Marshals (dates, horizon, step) into StepFromFrequency's (step_months,
// step_micros) pair and calls ComputeFutureDates — the same Rust calendar
// rule ts_forecast_by/ts_forecast_agg already use (plan 12-05), but reachable
// from a macro (ts_forecast_exog_by) instead of a table/aggregate function.
// Any NULL argument (dates/horizon/step) yields a NULL row; NULL entries
// inside `dates` are skipped (observed = non-NULL dates only). An empty
// observed list yields a NULL row. horizon <= 0 yields an empty list;
// horizon > 100000 throws before any allocation (T-12-21). A mixed step
// (INTERVAL with both months and days/micros non-zero) is rejected by the
// Rust FFI (anofox_ts_future_dates INVALID_INPUT), surfaced here as
// InvalidInputException via ComputeFutureDates.
// ============================================================================

static constexpr int64_t kMaxFutureDatesHorizon = 100000;

static void TsFutureDatesExecute(DataChunk &args, ExpressionState &state, Vector &result) {
    idx_t count = args.size();

    auto &dates_vec = args.data[0];    // LIST(TIMESTAMP)
    auto &horizon_vec = args.data[1];  // INTEGER
    auto &step_vec = args.data[2];     // INTERVAL

    result.SetVectorType(VectorType::FLAT_VECTOR);

    UnifiedVectorFormat dates_list_data, dates_child_data;
    dates_vec.ToUnifiedFormat(count, dates_list_data);
    auto &dates_child = ListVector::GetEntry(dates_vec);
    dates_child.ToUnifiedFormat(ListVector::GetListSize(dates_vec), dates_child_data);
    auto dates_entries = UnifiedVectorFormat::GetData<list_entry_t>(dates_list_data);
    auto dates_values = UnifiedVectorFormat::GetData<timestamp_t>(dates_child_data);

    ScalarArgReader<int32_t> horizon_reader(horizon_vec, count);
    ScalarArgReader<interval_t> step_reader(step_vec, count);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto dates_idx = dates_list_data.sel->get_index(row_idx);
        if (!dates_list_data.validity.RowIsValid(dates_idx) || horizon_reader.IsNull(row_idx) ||
            step_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        int32_t horizon = horizon_reader.Get(row_idx);
        if (horizon <= 0) {
            auto list_entries = FlatVector::GetData<list_entry_t>(result);
            list_entries[row_idx].offset = ListVector::GetListSize(result);
            list_entries[row_idx].length = 0;
            continue;
        }
        if (horizon > kMaxFutureDatesHorizon) {
            throw InvalidInputException("_ts_future_dates: horizon %d exceeds %lld", horizon,
                                         (long long)kMaxFutureDatesHorizon);
        }

        interval_t step = step_reader.Get(row_idx);
        int64_t step_months = step.months;
        int64_t step_micros = static_cast<int64_t>(step.days) * 86400LL * 1000000LL + step.micros;

        auto &dates_entry = dates_entries[dates_idx];
        vector<int64_t> observed;
        observed.reserve(dates_entry.length);
        for (idx_t i = 0; i < dates_entry.length; i++) {
            idx_t child_idx = dates_entry.offset + i;
            auto unified_idx = dates_child_data.sel->get_index(child_idx);
            if (dates_child_data.validity.RowIsValid(unified_idx)) {
                observed.push_back(TimestampToMicroseconds(dates_values[unified_idx]));
            }
        }

        if (observed.empty()) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<int64_t> future_ts = ComputeFutureDates(observed, horizon, step_months, step_micros);

        idx_t offset = ListVector::GetListSize(result);
        ListVector::Reserve(result, offset + future_ts.size());
        ListVector::SetListSize(result, offset + future_ts.size());
        auto &result_child = ListVector::GetEntry(result);
        auto result_child_data = FlatVector::GetData<timestamp_t>(result_child);
        for (size_t i = 0; i < future_ts.size(); i++) {
            result_child_data[offset + i] = MicrosecondsToTimestamp(future_ts[i]);
        }
        auto list_entries = FlatVector::GetData<list_entry_t>(result);
        list_entries[row_idx].offset = offset;
        list_entries[row_idx].length = future_ts.size();
    }
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsForecastScalarFunction(ExtensionLoader &loader) {
    // _ts_forecast_scalar(dates LIST, values LIST(DOUBLE), horizon INT,
    //                     frequency VARCHAR, method VARCHAR, params MAP)
    // -> LIST(STRUCT(forecast_step INT, ds ANY, yhat DOUBLE, ...))
    ScalarFunction func("_ts_forecast_scalar",
        {LogicalType::LIST(LogicalType::ANY),     // dates
         LogicalType::LIST(LogicalType::DOUBLE),  // values
         LogicalType::INTEGER,                     // horizon
         LogicalType::VARCHAR,                     // frequency
         LogicalType::VARCHAR,                     // method
         LogicalType::ANY},                        // params (MAP)
        LogicalType::LIST(LogicalType::ANY),       // return type set by bind
        TsForecastScalarExecute,
        TsForecastScalarBind);

    func.null_handling = FunctionNullHandling::SPECIAL_HANDLING;

    loader.RegisterFunction(func);

    // _ts_future_dates(dates LIST(TIMESTAMP), horizon INTEGER, step INTERVAL)
    // -> LIST(TIMESTAMP) — internal scalar backing ts_forecast_exog_by's
    // future-date computation (plan 12-06).
    ScalarFunction future_dates_func("_ts_future_dates",
        {LogicalType::LIST(LogicalType::TIMESTAMP), LogicalType::INTEGER, LogicalType::INTERVAL},
        LogicalType::LIST(LogicalType::TIMESTAMP), TsFutureDatesExecute);
    future_dates_func.null_handling = FunctionNullHandling::SPECIAL_HANDLING;

    CreateScalarFunctionInfo future_dates_info(future_dates_func);
    future_dates_info.internal = true;
    loader.RegisterFunction(future_dates_info);
}

} // namespace duckdb
