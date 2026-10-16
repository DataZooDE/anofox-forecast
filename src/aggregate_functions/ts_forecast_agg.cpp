#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "ts_forecast_params.hpp"
#include "ts_future_dates.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/parser/parsed_data/create_aggregate_function_info.hpp"
#include <cmath>

namespace duckdb {

// Strict weak ordering on doubles that is safe for std::sort (NaN sorts after
// every number). WR-02: same comparator as ts_stats.cpp's StatsValueLess,
// used as a deterministic secondary sort key for duplicate timestamps.
static bool ForecastAggValueLess(double x, double y) {
    const bool x_nan = std::isnan(x);
    const bool y_nan = std::isnan(y);
    if (x_nan || y_nan) {
        return !x_nan && y_nan;
    }
    return x < y;
}

// Internal state class (allocated on heap)
struct TsForecastAggStateData {
    vector<int64_t> timestamps;
    vector<double> values;
    // D-11: validity bitmask parallel to `values` (true = non-NULL value at
    // that timestamp). A NULL-valued row is no longer dropped at Update time
    // (only a NULL timestamp is dropped) so the forecast FFI's NULL policy
    // (trim leading/trailing, interpolate interior, anchor origin after the
    // original last row) runs exactly as it does for ts_forecast_by, instead
    // of ts_forecast_agg silently splicing NULL-valued rows out before the
    // policy ever sees them.
    vector<bool> valid;
    string method;
    int32_t horizon;
    // 10-10 (FCST-06): single shared params set — validated and threaded
    // through ApplyForecastParamSet exactly like ts_forecast_by, instead of
    // the five bespoke fields (ets_model/confidence_level/arima_order/
    // seasonal_order/seasonal_period) this struct used to carry, each of
    // which silently dropped every other ts_forecast_by param.
    ForecastParamSet params;
    bool initialized;

    TsForecastAggStateData() : horizon(12), initialized(false) {}
};

// Trivially constructible state wrapper (just a pointer)
struct TsForecastAggState {
    TsForecastAggStateData *data;
};

// Bind data for dynamic column naming
struct TsForecastAggBindData : public FunctionData {
    double confidence_level;
    string lower_col_name;
    string upper_col_name;

    TsForecastAggBindData() : confidence_level(0.90) {
        UpdateColumnNames();
    }

    void UpdateColumnNames() {
        // Convert confidence level to percentage suffix (e.g., 0.90 -> "90", 0.95 -> "95")
        int pct = static_cast<int>(std::round(confidence_level * 100));
        lower_col_name = "lower_" + std::to_string(pct);
        upper_col_name = "upper_" + std::to_string(pct);
    }

    unique_ptr<FunctionData> Copy() const override {
        auto result = make_uniq<TsForecastAggBindData>();
        result->confidence_level = confidence_level;
        result->lower_col_name = lower_col_name;
        result->upper_col_name = upper_col_name;
        return result;
    }

    bool Equals(const FunctionData &other_p) const override {
        auto &other = other_p.Cast<TsForecastAggBindData>();
        return confidence_level == other.confidence_level;
    }
};

// Helper to get confidence level suffix
static string GetConfidenceSuffix(double confidence_level) {
    int pct = static_cast<int>(std::round(confidence_level * 100));
    return std::to_string(pct);
}

// Build result type with dynamic column names based on confidence level
static LogicalType GetForecastAggResultType(double confidence_level = 0.90) {
    string suffix = GetConfidenceSuffix(confidence_level);

    child_list_t<LogicalType> children;
    children.push_back(make_pair("forecast_step", LogicalType::LIST(LogicalType(LogicalTypeId::INTEGER))));
    children.push_back(make_pair("forecast_timestamp", LogicalType::LIST(LogicalType(LogicalTypeId::TIMESTAMP))));
    children.push_back(make_pair("point_forecast", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("lower_" + suffix, LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("upper_" + suffix, LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("model_name", LogicalType(LogicalTypeId::VARCHAR)));
    children.push_back(make_pair("insample_fitted", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    // WR-02: these four were promised by desc.description below but never
    // actually returned — the crate ForecastResult already carries
    // residuals/aic/bic/mse (same fcst_result this Finalize already
    // populates for point/lower/upper/fitted), so wiring them through is
    // trivial and keeps the STRUCT truthful instead of just rewording the
    // description.
    children.push_back(make_pair("residuals", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("aic", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("bic", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("mse", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("date_col_name", LogicalType(LogicalTypeId::VARCHAR)));
    children.push_back(make_pair("error_message", LogicalType(LogicalTypeId::VARCHAR)));
    return LogicalType::STRUCT(std::move(children));
}

// Bind function to extract confidence_level from params MAP and set dynamic return type.
//
// 10-10 (FCST-06, D-01): confidence_level now DOES come from the params MAP
// when it is a bind-time constant (the common case — `MAP{'confidence_level':
// '0.95'}` literal), via the same ExpressionExecutor::EvaluateScalar
// bind-time fold `string_agg`/`bitstring_agg` use for their own constant
// arguments (duckdb/extension/core_functions/aggregate/distributive/
// string_agg.cpp:135). A non-constant per-row confidence_level is still
// readable at Update time, but would require the STRUCT's lower_<pct>/
// upper_<pct> field names to vary per group, which DuckDB's aggregate
// framework cannot express (the return type is fixed at bind). So
// TsForecastAggUpdate (below) throws if a group's parsed confidence_level
// disagrees with the one folded here — "confidence_level must be a constant
// expression" — rather than silently mislabelling the bound fields.
static unique_ptr<FunctionData> TsForecastAggBind(ClientContext &context, AggregateFunction &function,
                                                   vector<unique_ptr<Expression>> &arguments) {
    auto bind_data = make_uniq<TsForecastAggBindData>();

    // Default confidence level
    bind_data->confidence_level = 0.90;

    if (arguments.size() > 4 && arguments[4]->IsFoldable()) {
        Value params_value = ExpressionExecutor::EvaluateScalar(context, *arguments[4]);
        if (!params_value.IsNull()) {
            // Fails fast at bind time for unknown keys even when the
            // params MAP is otherwise empty/constant (10-VERIFICATION.md
            // gap: this surface previously never validated at all).
            ValidateForecastParamKeys(params_value);
            bind_data->confidence_level = ForecastParamDouble(params_value, "confidence_level", 0.90);
        }
    }

    bind_data->UpdateColumnNames();

    // Set the return type with dynamic column names
    function.return_type = GetForecastAggResultType(bind_data->confidence_level);

    return bind_data;
}

struct TsForecastAggOperation {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.data = nullptr;
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.data || !source.data->initialized) {
            return;
        }
        if (!target.data) {
            target.data = new TsForecastAggStateData();
        }
        if (!target.data->initialized) {
            *target.data = *source.data;
        } else {
            target.data->timestamps.insert(target.data->timestamps.end(),
                                           source.data->timestamps.begin(),
                                           source.data->timestamps.end());
            target.data->values.insert(target.data->values.end(),
                                       source.data->values.begin(),
                                       source.data->values.end());
            target.data->valid.insert(target.data->valid.end(),
                                      source.data->valid.begin(),
                                      source.data->valid.end());
        }
    }

    static bool IgnoreNull() {
        return true;
    }
};

static void TsForecastAggUpdate(Vector inputs[], AggregateInputData &aggr_input, idx_t input_count,
                                Vector &state_vector, idx_t count) {
    auto &ts_vec = inputs[0];
    auto &val_vec = inputs[1];
    auto &method_vec = inputs[2];
    auto &horizon_vec = inputs[3];
    auto &params_vec = inputs[4];  // params MAP

    UnifiedVectorFormat ts_data, val_data, method_data, horizon_data, params_data;
    ts_vec.ToUnifiedFormat(count, ts_data);
    val_vec.ToUnifiedFormat(count, val_data);
    method_vec.ToUnifiedFormat(count, method_data);
    horizon_vec.ToUnifiedFormat(count, horizon_data);
    params_vec.ToUnifiedFormat(count, params_data);

    auto states = FlatVector::GetData<TsForecastAggState *>(state_vector);

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];

        auto ts_idx = ts_data.sel->get_index(i);
        auto val_idx = val_data.sel->get_index(i);
        auto method_idx = method_data.sel->get_index(i);
        auto horizon_idx = horizon_data.sel->get_index(i);

        // D-11: only a NULL timestamp drops the row. A NULL value is kept
        // (stored as 0.0 with valid=false) so Finalize's validity bitmask
        // can feed the shared forecasting NULL policy instead of splicing
        // the row out before the policy ever runs.
        if (!ts_data.validity.RowIsValid(ts_idx)) {
            continue;
        }

        if (!state.data) {
            state.data = new TsForecastAggStateData();
        }

        if (!state.data->initialized) {
            if (method_data.validity.RowIsValid(method_idx)) {
                state.data->method = UnifiedVectorFormat::GetData<string_t>(method_data)[method_idx].GetString();
            } else {
                state.data->method = "auto";
            }
            if (horizon_data.validity.RowIsValid(horizon_idx)) {
                state.data->horizon = UnifiedVectorFormat::GetData<int32_t>(horizon_data)[horizon_idx];
            } else {
                state.data->horizon = 12;
            }

            // 10-10 (FCST-06): validate the closed params key set and parse
            // every ts_forecast_by param once per group, via the exact same
            // ParseForecastParamSet helper _ts_forecast_scalar (ts_forecast_by)
            // uses — this is the fix for the gap where ts_forecast_agg used
            // to read only model/arima_order/seasonal_order/seasonal_period
            // from the MAP and silently drop everything else (kalman_model,
            // garch_p/garch_q, model_pool, window, laplace_*, top_k,
            // combination_method, confidence_level). Also applies WR-03's
            // arima_order/seasonal_order length guard (inside
            // ParseForecastParamSet) per group.
            {
                auto params_idx = params_data.sel->get_index(i);
                Value params_value =
                    params_data.validity.RowIsValid(params_idx) ? params_vec.GetValue(i) : Value();
                state.data->params = ParseForecastParamSet(params_value);
            }

            // confidence_level determines the bound STRUCT's lower_<pct>/
            // upper_<pct> field names, which TsForecastAggBind fixes at bind
            // time by folding this same argument (when it is a constant
            // expression). A per-group value that disagrees with the folded
            // one would silently mislabel those already-bound field names,
            // so raise instead of letting it pass quietly (D-01).
            {
                auto &bind_data = aggr_input.bind_data->Cast<TsForecastAggBindData>();
                if (std::abs(state.data->params.confidence_level - bind_data.confidence_level) > 1e-9) {
                    throw InvalidInputException(
                        "ts_forecast_agg: confidence_level must be a constant expression (bound confidence_level "
                        "= %f from the params argument's bind-time value, but this group's params resolved "
                        "confidence_level = %f) — the lower_<pct>/upper_<pct> field names are fixed at bind time",
                        bind_data.confidence_level, state.data->params.confidence_level);
                }
            }

            state.data->initialized = true;
        }

        auto ts = UnifiedVectorFormat::GetData<timestamp_t>(ts_data)[ts_idx];

        state.data->timestamps.push_back(ts.value);
        bool value_is_valid = val_data.validity.RowIsValid(val_idx);
        if (value_is_valid) {
            auto val = UnifiedVectorFormat::GetData<double>(val_data)[val_idx];
            state.data->values.push_back(val);
        } else {
            state.data->values.push_back(0.0);
        }
        state.data->valid.push_back(value_is_valid);
    }
}

static void TsForecastAggFinalize(Vector &state_vector, AggregateInputData &aggr_input,
                                  Vector &result, idx_t count, idx_t offset) {
    auto states = FlatVector::GetData<TsForecastAggState *>(state_vector);

    auto &children = StructVector::GetEntries(result);
    auto &step_list = *children[0];      // forecast_step
    auto &ts_list = *children[1];        // forecast_timestamp
    auto &point_list = *children[2];     // point_forecast
    auto &lower_list = *children[3];     // lower_<suffix>
    auto &upper_list = *children[4];     // upper_<suffix>
    auto &model_vec = *children[5];      // model_name
    auto &fitted_list = *children[6];    // insample_fitted
    auto &residuals_list = *children[7]; // residuals (WR-02)
    auto &aic_vec = *children[8];        // aic (WR-02)
    auto &bic_vec = *children[9];        // bic (WR-02)
    auto &mse_vec = *children[10];       // mse (WR-02)
    auto &date_col_vec = *children[11];  // date_col_name
    auto &error_vec = *children[12];     // error_message

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];
        idx_t row = i + offset;

        if (!state.data || !state.data->initialized || state.data->values.empty()) {
            FlatVector::SetNull(result, row, true);
            continue;
        }

        auto &data = *state.data;

        // Sort (timestamp, value, valid) together by timestamp (D-11/D-12):
        // an index sort, not a sort over pair<timestamp,value>, so the
        // per-row validity flag stays aligned with its value through the
        // reorder. WR-02: duplicate timestamps are tie-broken deterministically
        // (valid before NULL, then by value with NaN last), the same
        // comparator ts_stats.cpp's StatsValueLess/TsStatsByFinalize uses, so
        // the result never depends on the order partial aggregate states were
        // concatenated in Combine() (which itself depends on thread
        // scheduling) — std::sort by timestamp alone left the relative order
        // of same-timestamp rows unspecified.
        vector<size_t> order(data.timestamps.size());
        for (size_t j = 0; j < order.size(); j++) {
            order[j] = j;
        }
        std::sort(order.begin(), order.end(), [&data](size_t a, size_t b) {
            if (data.timestamps[a] != data.timestamps[b]) {
                return data.timestamps[a] < data.timestamps[b];
            }
            const bool a_valid = data.valid[a];
            const bool b_valid = data.valid[b];
            if (a_valid != b_valid) {
                return a_valid;
            }
            return a_valid && ForecastAggValueLess(data.values[a], data.values[b]);
        });

        vector<double> sorted_values;
        vector<int64_t> sorted_timestamps;
        vector<bool> sorted_valid;
        sorted_values.reserve(order.size());
        sorted_timestamps.reserve(order.size());
        sorted_valid.reserve(order.size());
        for (size_t idx : order) {
            sorted_timestamps.push_back(data.timestamps[idx]);
            sorted_values.push_back(data.values[idx]);
            sorted_valid.push_back(data.valid[idx]);
        }

        // Real validity bitmask (D-11) — no longer an all-ones `~0ULL`
        // placeholder, since NULL-valued rows are kept (not spliced out) by
        // Update above. anofox_ts_forecast applies the same NULL policy
        // ts_forecast_by uses given this bitmask: trim leading/trailing,
        // interpolate interior, anchor step 1 after the original last row.
        size_t validity_words = (sorted_values.size() + 63) / 64;
        vector<uint64_t> validity(validity_words, 0);
        for (size_t j = 0; j < sorted_valid.size(); j++) {
            if (sorted_valid[j]) {
                validity[j / 64] |= (1ULL << (j % 64));
            }
        }

        // 10-10 (FCST-06): the exact same ApplyForecastParamSet writer
        // _ts_forecast_scalar (ts_forecast_by) uses, applied to the params
        // this group's TsForecastAggUpdate already validated and parsed —
        // kalman_model, garch_p/garch_q, model_pool, window, laplace_*,
        // top_k, combination_method, seasonal_periods and arima_order/
        // seasonal_order/seasonal_period now all thread through, instead of
        // only arima_order/seasonal_order/seasonal_period as before.
        ForecastOptions opts;
        memset(&opts, 0, sizeof(opts));
        ApplyForecastParamSet(data.params, data.method, opts);
        opts.horizon = data.horizon;
        opts.include_fitted = true;
        opts.include_residuals = true;  // WR-02: residuals[] is now a real output field

        ForecastResult fcst_result;
        memset(&fcst_result, 0, sizeof(fcst_result));
        AnofoxError error;

        bool success = anofox_ts_forecast(
            sorted_values.data(),
            validity.data(),
            sorted_values.size(),
            &opts,
            &fcst_result,
            &error
        );

        if (!success) {
            // 10-10 (FCST-06/D-07): invalid model/parameter values (e.g. an
            // unrecognized kalman_model, or 'ARIMA' without arima_order) now
            // raise the same InvalidInputException ts_forecast_by's scalar
            // path raises for the identical input (ts_forecast_scalar.cpp's
            // INVALID_MODEL/INVALID_INPUT rule) instead of only ever being
            // reported in this group's error_message field. Every other
            // error code keeps the pre-existing error_message + empty-lists
            // + NULL aic/bic/mse behavior unchanged.
            if (error.code == INVALID_MODEL || error.code == INVALID_INPUT) {
                throw InvalidInputException(string(error.message));
            }

            // Set error message instead of returning null
            FlatVector::GetData<string_t>(error_vec)[row] =
                StringVector::AddString(error_vec, error.message);
            // Set empty lists for other fields
            auto set_empty_list = [row](Vector &list_vec) {
                auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
                list_data[row].offset = ListVector::GetListSize(list_vec);
                list_data[row].length = 0;
            };
            set_empty_list(step_list);
            set_empty_list(ts_list);
            set_empty_list(point_list);
            set_empty_list(lower_list);
            set_empty_list(upper_list);
            set_empty_list(fitted_list);
            set_empty_list(residuals_list);
            FlatVector::SetNull(aic_vec, row, true);
            FlatVector::SetNull(bic_vec, row, true);
            FlatVector::SetNull(mse_vec, row, true);
            FlatVector::GetData<string_t>(model_vec)[row] =
                StringVector::AddString(model_vec, "");
            FlatVector::GetData<string_t>(date_col_vec)[row] =
                StringVector::AddString(date_col_vec, "date");
            continue;
        }

        // Compute forecast timestamps via the Rust D-12 calendar rule
        // (month-end-preserving / day-of-month-clamp stepping for monthly/
        // quarterly/yearly data, deterministic frequency detection) instead
        // of the old last_ts + k*median_step arithmetic, which drifted off
        // month-end on short months (D-12). step_months=0, step_micros=0
        // requests Rust auto-detection (anofox_fcst_core::detect_step).
        vector<int64_t> future_ts = ComputeFutureDates(sorted_timestamps, fcst_result.n_forecasts, 0, 0);

        // Set forecast_step list
        {
            auto list_data = FlatVector::GetData<list_entry_t>(step_list);
            auto &list_child = ListVector::GetEntry(step_list);
            auto current_size = ListVector::GetListSize(step_list);

            list_data[row].offset = current_size;
            list_data[row].length = fcst_result.n_forecasts;

            ListVector::Reserve(step_list, current_size + fcst_result.n_forecasts);
            ListVector::SetListSize(step_list, current_size + fcst_result.n_forecasts);

            auto child_data = FlatVector::GetData<int32_t>(list_child);
            for (size_t j = 0; j < fcst_result.n_forecasts; j++) {
                child_data[current_size + j] = j + 1;
            }
        }

        // Set forecast_timestamp list
        {
            auto list_data = FlatVector::GetData<list_entry_t>(ts_list);
            auto &list_child = ListVector::GetEntry(ts_list);
            auto current_size = ListVector::GetListSize(ts_list);

            list_data[row].offset = current_size;
            list_data[row].length = fcst_result.n_forecasts;

            ListVector::Reserve(ts_list, current_size + fcst_result.n_forecasts);
            ListVector::SetListSize(ts_list, current_size + fcst_result.n_forecasts);

            auto child_data = FlatVector::GetData<timestamp_t>(list_child);
            for (size_t j = 0; j < fcst_result.n_forecasts; j++) {
                child_data[current_size + j] = timestamp_t(future_ts[j]);
            }
        }

        // Set point_forecast list
        {
            auto list_data = FlatVector::GetData<list_entry_t>(point_list);
            auto &list_child = ListVector::GetEntry(point_list);
            auto current_size = ListVector::GetListSize(point_list);

            list_data[row].offset = current_size;
            list_data[row].length = fcst_result.n_forecasts;

            ListVector::Reserve(point_list, current_size + fcst_result.n_forecasts);
            ListVector::SetListSize(point_list, current_size + fcst_result.n_forecasts);

            auto child_data = FlatVector::GetData<double>(list_child);
            memcpy(child_data + current_size, fcst_result.point_forecasts,
                   fcst_result.n_forecasts * sizeof(double));
        }

        // Set lower_bound list. lower_bounds is null when the model has no
        // native prediction intervals (D-02) — append n_forecasts entries
        // marked invalid (SQL NULL) instead of memcpy-ing from a null
        // pointer.
        {
            auto list_data = FlatVector::GetData<list_entry_t>(lower_list);
            auto &list_child = ListVector::GetEntry(lower_list);
            auto current_size = ListVector::GetListSize(lower_list);

            list_data[row].offset = current_size;
            list_data[row].length = fcst_result.n_forecasts;

            ListVector::Reserve(lower_list, current_size + fcst_result.n_forecasts);
            ListVector::SetListSize(lower_list, current_size + fcst_result.n_forecasts);

            auto child_data = FlatVector::GetData<double>(list_child);
            if (fcst_result.lower_bounds) {
                memcpy(child_data + current_size, fcst_result.lower_bounds,
                       fcst_result.n_forecasts * sizeof(double));
            } else {
                auto &child_validity = FlatVector::Validity(list_child);
                for (size_t j = 0; j < fcst_result.n_forecasts; j++) {
                    child_data[current_size + j] = 0.0;
                    child_validity.SetInvalid(current_size + j);
                }
            }
        }

        // Set upper_<suffix> list. Same null-safety as lower_bound above.
        {
            auto list_data = FlatVector::GetData<list_entry_t>(upper_list);
            auto &list_child = ListVector::GetEntry(upper_list);
            auto current_size = ListVector::GetListSize(upper_list);

            list_data[row].offset = current_size;
            list_data[row].length = fcst_result.n_forecasts;

            ListVector::Reserve(upper_list, current_size + fcst_result.n_forecasts);
            ListVector::SetListSize(upper_list, current_size + fcst_result.n_forecasts);

            auto child_data = FlatVector::GetData<double>(list_child);
            if (fcst_result.upper_bounds) {
                memcpy(child_data + current_size, fcst_result.upper_bounds,
                       fcst_result.n_forecasts * sizeof(double));
            } else {
                auto &child_validity = FlatVector::Validity(list_child);
                for (size_t j = 0; j < fcst_result.n_forecasts; j++) {
                    child_data[current_size + j] = 0.0;
                    child_validity.SetInvalid(current_size + j);
                }
            }
        }

        // Set model_name
        FlatVector::GetData<string_t>(model_vec)[row] =
            StringVector::AddString(model_vec, fcst_result.model_name);

        // Set fitted values list. NaN entries (crate warmup rows per D-11, or
        // leading/trailing trimmed positions per D-12) mark SQL NULL rather
        // than surfacing as a literal NaN double.
        {
            auto list_data = FlatVector::GetData<list_entry_t>(fitted_list);
            auto &list_child = ListVector::GetEntry(fitted_list);
            auto current_size = ListVector::GetListSize(fitted_list);

            list_data[row].offset = current_size;
            list_data[row].length = fcst_result.n_fitted;

            ListVector::Reserve(fitted_list, current_size + fcst_result.n_fitted);
            ListVector::SetListSize(fitted_list, current_size + fcst_result.n_fitted);

            auto child_data = FlatVector::GetData<double>(list_child);
            if (fcst_result.fitted_values && fcst_result.n_fitted > 0) {
                memcpy(child_data + current_size, fcst_result.fitted_values,
                       fcst_result.n_fitted * sizeof(double));
                auto &child_validity = FlatVector::Validity(list_child);
                for (size_t j = 0; j < fcst_result.n_fitted; j++) {
                    if (std::isnan(child_data[current_size + j])) {
                        child_validity.SetInvalid(current_size + j);
                    }
                }
            }
        }

        // Set residuals list (WR-02). Same NaN-as-SQL-NULL policy as
        // insample_fitted above, and the same n_residuals (not n_fitted —
        // see the FFI header's n_residuals doc comment) length field the
        // non-aggregate ts_forecast table/scalar functions use.
        {
            auto list_data = FlatVector::GetData<list_entry_t>(residuals_list);
            auto &list_child = ListVector::GetEntry(residuals_list);
            auto current_size = ListVector::GetListSize(residuals_list);

            list_data[row].offset = current_size;
            list_data[row].length = fcst_result.n_residuals;

            ListVector::Reserve(residuals_list, current_size + fcst_result.n_residuals);
            ListVector::SetListSize(residuals_list, current_size + fcst_result.n_residuals);

            auto child_data = FlatVector::GetData<double>(list_child);
            if (fcst_result.residuals && fcst_result.n_residuals > 0) {
                memcpy(child_data + current_size, fcst_result.residuals,
                       fcst_result.n_residuals * sizeof(double));
                auto &child_validity = FlatVector::Validity(list_child);
                for (size_t j = 0; j < fcst_result.n_residuals; j++) {
                    if (std::isnan(child_data[current_size + j])) {
                        child_validity.SetInvalid(current_size + j);
                    }
                }
            }
        }

        // Set aic/bic/mse (WR-02) — straight from the crate fit, same as
        // ts_forecast.cpp's scalar/table surfaces.
        FlatVector::GetData<double>(aic_vec)[row] = fcst_result.aic;
        FlatVector::GetData<double>(bic_vec)[row] = fcst_result.bic;
        FlatVector::GetData<double>(mse_vec)[row] = fcst_result.mse;

        // Set date_col_name (default to "date" as we don't have the column name in aggregate context)
        FlatVector::GetData<string_t>(date_col_vec)[row] =
            StringVector::AddString(date_col_vec, "date");

        // Set error_message (empty on success)
        FlatVector::GetData<string_t>(error_vec)[row] =
            StringVector::AddString(error_vec, "");

        anofox_free_forecast_result(&fcst_result);
    }
}

static void TsForecastAggCombine(Vector &state_vector, Vector &combined, AggregateInputData &aggr_input,
                                 idx_t count) {
    auto src_states = FlatVector::GetData<TsForecastAggState *>(state_vector);
    auto tgt_states = FlatVector::GetData<TsForecastAggState *>(combined);

    for (idx_t i = 0; i < count; i++) {
        auto &src = *src_states[i];
        auto &tgt = *tgt_states[i];

        if (!src.data || !src.data->initialized) {
            continue;
        }

        if (!tgt.data) {
            tgt.data = new TsForecastAggStateData();
            *tgt.data = *src.data;
        } else if (!tgt.data->initialized) {
            *tgt.data = *src.data;
        } else {
            tgt.data->timestamps.insert(tgt.data->timestamps.end(),
                                        src.data->timestamps.begin(),
                                        src.data->timestamps.end());
            tgt.data->values.insert(tgt.data->values.end(),
                                    src.data->values.begin(),
                                    src.data->values.end());
            tgt.data->valid.insert(tgt.data->valid.end(),
                                   src.data->valid.begin(),
                                   src.data->valid.end());
        }
    }
}

static void TsForecastAggDestructor(Vector &state_vector, AggregateInputData &aggr_input, idx_t count) {
    auto states = FlatVector::GetData<TsForecastAggState *>(state_vector);
    for (idx_t i = 0; i < count; i++) {
        if (states[i] && states[i]->data) {
            delete states[i]->data;
            states[i]->data = nullptr;
        }
    }
}

void RegisterTsForecastAggFunction(ExtensionLoader &loader) {
    // Create aggregate function with 5 parameters matching API spec:
    // (date_col, value_col, method, horizon, params)
    AggregateFunction agg_func(
        "anofox_fcst_ts_forecast_agg",
        {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::INTEGER),
         LogicalType::MAP(LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::VARCHAR))},
        GetForecastAggResultType(),
        AggregateFunction::StateSize<TsForecastAggState>,
        AggregateFunction::StateInitialize<TsForecastAggState, TsForecastAggOperation>,
        TsForecastAggUpdate,
        TsForecastAggCombine,
        TsForecastAggFinalize,
        nullptr,  // simple_update
        TsForecastAggBind,
        TsForecastAggDestructor
    );

    AggregateFunctionSet func_set("anofox_fcst_ts_forecast_agg");
    func_set.AddFunction(agg_func);
    {
        CreateAggregateFunctionInfo info(func_set);
        info.alias_of = "ts_forecast_agg";
        FunctionDescription desc;
        desc.description = "Aggregate function: produces a time series forecast for each group. Supports 32 forecasting models. Returns STRUCT(point[], lower[], upper[], fitted[], residuals[], model, aic, bic, mse).";
        desc.examples = {"SELECT product_id, anofox_fcst_ts_forecast_agg(date, value, 'AutoETS', 12, MAP{}) FROM sales GROUP BY product_id"};
        desc.categories = {"time-series", "forecasting"};
        desc.parameter_names = {"date", "value", "method", "horizon", "params"};
        desc.parameter_types = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::INTEGER), LogicalType::MAP(LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::VARCHAR))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    // Also register as ts_forecast_agg alias
    AggregateFunctionSet alias_set("ts_forecast_agg");
    alias_set.AddFunction(agg_func);
    {
        CreateAggregateFunctionInfo info(alias_set);
        FunctionDescription desc;
        desc.description = "Aggregate function: produces a time series forecast for each group. Supports 32 forecasting models. Returns STRUCT(point[], lower[], upper[], fitted[], residuals[], model, aic, bic, mse).";
        desc.examples = {"SELECT product_id, ts_forecast_agg(date, value, 'AutoETS', 12, MAP{}) FROM sales GROUP BY product_id"};
        desc.categories = {"time-series", "forecasting"};
        desc.parameter_names = {"date", "value", "method", "horizon", "params"};
        desc.parameter_types = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE), LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::INTEGER), LogicalType::MAP(LogicalType(LogicalTypeId::VARCHAR), LogicalType(LogicalTypeId::VARCHAR))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

} // namespace duckdb
