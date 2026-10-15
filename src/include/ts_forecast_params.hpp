#pragma once

// ============================================================================
// ts_forecast_params.hpp — shared params-MAP parser/writer for ts_forecast_by
// (_ts_forecast_scalar) and ts_forecast_agg (gap-closure plan 10-10, FCST-06).
//
// Both the scalar path behind `ts_forecast_by` and the aggregate path
// `ts_forecast_agg` previously carried their own (divergent) params MAP
// handling; the aggregate's copy silently dropped every key except
// `model`/`arima_order`/`seasonal_order`/`seasonal_period` and hardcoded
// `confidence_level` to 0.90 (10-VERIFICATION.md gap / FCST-06). This header
// is the single source of truth for the closed params key set, the per-key
// parse helpers and the `ForecastOptions` writer, so the two surfaces cannot
// drift again. `ts_forecast_native.cpp`, `ts_cv_forecast_native.cpp` and
// `ts_forecast.cpp` (the exog path) keep their own (identical) copies of the
// same key set — out of this gap's scope; see 10-10-PLAN.md.
// ============================================================================

#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include <cstring>
#include <unordered_set>

namespace duckdb {

// Parsed, validated params-MAP (or STRUCT) contents. Defaults mirror the
// per-row locals TsForecastScalarExecute used before this header existed.
struct ForecastParamSet {
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
    int64_t ensemble_top_k = 0;
    string ensemble_method = "";
    string arima_order = "";
    string seasonal_order = "";
};

// Verbatim semantics of the former ts_forecast_scalar.cpp ParseStringParam:
// accepts MAP(VARCHAR, VARCHAR) and typed STRUCT params; missing key or NULL
// value falls back to default_val.
inline string ForecastParamString(const Value &params_value, const string &key, const string &default_val) {
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
        // Mirrors ts_forecast_native.cpp — accept typed STRUCT params too,
        // so `{seasonal_period: 7, laplace_variant: 'auto'}` behaves the
        // same as the equivalent MAP.
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

// Verbatim semantics of the former ParseInt64Param: unparsable numbers fall
// back to default_val rather than erroring (pre-existing leniency, kept for
// ts_forecast_by/ts_forecast_agg parity — see deferred-items.md entry 3).
inline int64_t ForecastParamInt64(const Value &params_value, const string &key, int64_t default_val) {
    string str = ForecastParamString(params_value, key, "");
    if (str.empty()) {
        return default_val;
    }
    try {
        return std::stoll(str);
    } catch (...) {
        return default_val;
    }
}

// Verbatim semantics of the former ParseDoubleParam (same leniency note).
inline double ForecastParamDouble(const Value &params_value, const string &key, double default_val) {
    string str = ForecastParamString(params_value, key, "");
    if (str.empty()) {
        return default_val;
    }
    try {
        return std::stod(str);
    } catch (...) {
        return default_val;
    }
}

// The 15-key closed set every forecast surface (ts_forecast_native.cpp,
// ts_cv_forecast_native.cpp, ts_forecast.cpp's exog path, and now this
// shared header for ts_forecast_by/ts_forecast_agg) validates against.
// Message text is byte-identical to ts_forecast_native.cpp's
// ValidateParamKeys so "Unknown parameter(s): ..." reads the same on every
// surface.
inline void ValidateForecastParamKeys(const Value &params_value) {
    static const unordered_set<string> valid_keys = {
        "model", "seasonal_period", "seasonal_periods", "confidence_level", "window", "model_pool",
        "laplace_variant", "laplace_seasonal_batch_init",
        "garch_p", "garch_q", "kalman_model",
        "top_k", "combination_method",  // Phase 4: AutoEnsemble params
        "arima_order", "seasonal_order" // D-08/D-09, plan 10-02
    };

    if (params_value.IsNull()) {
        return;
    }

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
            if (i > 0) {
                unknown_list += ", ";
            }
            unknown_list += "'" + unknown_keys[i] + "'";
        }
        throw InvalidInputException(
            "Unknown parameter(s): %s. Valid parameters are: model, seasonal_period, seasonal_periods, confidence_level, window, model_pool, laplace_variant, laplace_seasonal_batch_init, garch_p, garch_q, kalman_model, top_k, combination_method, arima_order, seasonal_order",
            unknown_list);
    }
}

// NULL params -> defaults (no validation, no WR-03 checks — nothing to
// check). Non-NULL params -> validate the closed key set, parse every key,
// then apply the WR-03 "too long" guards before the caller ever copies
// arima_order/seasonal_order into the fixed 32-byte ForecastOptions buffers.
inline ForecastParamSet ParseForecastParamSet(const Value &params_value) {
    ForecastParamSet p;
    if (params_value.IsNull()) {
        return p;
    }

    ValidateForecastParamKeys(params_value);

    p.model_spec = ForecastParamString(params_value, "model", "");
    p.seasonal_period = ForecastParamInt64(params_value, "seasonal_period", 0);
    p.confidence_level = ForecastParamDouble(params_value, "confidence_level", 0.90);
    p.window = ForecastParamInt64(params_value, "window", 0);
    p.seasonal_periods_str = ForecastParamString(params_value, "seasonal_periods", "");
    p.model_pool = ForecastParamString(params_value, "model_pool", "");
    p.laplace_variant = ForecastParamString(params_value, "laplace_variant", "");
    p.laplace_seasonal_batch_init = ForecastParamInt64(params_value, "laplace_seasonal_batch_init", 0) != 0;
    p.garch_p = ForecastParamInt64(params_value, "garch_p", 0);
    p.garch_q = ForecastParamInt64(params_value, "garch_q", 0);
    p.kalman_model = ForecastParamString(params_value, "kalman_model", "");
    p.ensemble_top_k = ForecastParamInt64(params_value, "top_k", 0);
    p.ensemble_method = ForecastParamString(params_value, "combination_method", "");
    p.arima_order = ForecastParamString(params_value, "arima_order", "");
    p.seasonal_order = ForecastParamString(params_value, "seasonal_order", "");

    // WR-03: reject arima_order/seasonal_order strings that would silently
    // truncate when ApplyForecastParamSet below copies them into the fixed
    // 32-byte ForecastOptions::arima_order_str/seasonal_order_str buffers.
    if (p.arima_order.size() > sizeof(ForecastOptions::arima_order_str) - 1) {
        throw InvalidInputException(
            "arima_order value is too long (max %d characters): '%s'",
            (int)(sizeof(ForecastOptions::arima_order_str) - 1), p.arima_order);
    }
    if (p.seasonal_order.size() > sizeof(ForecastOptions::seasonal_order_str) - 1) {
        throw InvalidInputException(
            "seasonal_order value is too long (max %d characters): '%s'",
            (int)(sizeof(ForecastOptions::seasonal_order_str) - 1), p.seasonal_order);
    }

    return p;
}

// Writes every ForecastOptions field ParseForecastParamSet owns EXCEPT
// horizon / include_fitted / include_residuals — those are caller-owned
// (ts_forecast_by always wants fitted/residuals off; ts_forecast_agg always
// wants them on; horizon comes from a separate argument on both surfaces).
// Caller must memset opts to zero before calling this.
inline void ApplyForecastParamSet(const ForecastParamSet &p, const string &method, ForecastOptions &opts) {
    strncpy(opts.model, method.c_str(), sizeof(opts.model) - 1);
    opts.model[sizeof(opts.model) - 1] = '\0';

    // Validate 'model' param is only used with ETS method (#177). This mirrors
    // src/table_functions/ts_forecast_native.cpp's identical check -- that
    // function is not reached by ts_forecast_by/ts_forecast_agg (they call
    // _ts_forecast_scalar / this shared header instead), so without this check
    // here the param was silently accepted and ignored for non-ETS methods,
    // the exact "silent model/parameter fallback" this milestone eliminates
    // elsewhere (Phase 18, 18-01, Rule 2 fix; surfaced by
    // test/sql/ts_forecast_ets_model.test under the json gate).
    if (!p.model_spec.empty() && method != "ETS") {
        throw InvalidInputException(
            "Parameter 'model' (value: '%s') is only valid when method='ETS'. "
            "Current method is '%s'. Remove the 'model' parameter or change method to 'ETS'.",
            p.model_spec, method);
    }

    if (!p.model_spec.empty()) {
        strncpy(opts.ets_model, p.model_spec.c_str(), sizeof(opts.ets_model) - 1);
        opts.ets_model[sizeof(opts.ets_model) - 1] = '\0';
    }

    opts.confidence_level = p.confidence_level;
    opts.seasonal_period = static_cast<int>(p.seasonal_period);
    opts.auto_detect_seasonality = (p.seasonal_period == 0 && p.seasonal_periods_str.empty());
    opts.window = static_cast<int>(p.window);

    if (!p.seasonal_periods_str.empty()) {
        strncpy(opts.seasonal_periods_str, p.seasonal_periods_str.c_str(), sizeof(opts.seasonal_periods_str) - 1);
        opts.seasonal_periods_str[sizeof(opts.seasonal_periods_str) - 1] = '\0';
    }
    if (!p.model_pool.empty()) {
        strncpy(opts.model_pool, p.model_pool.c_str(), sizeof(opts.model_pool) - 1);
        opts.model_pool[sizeof(opts.model_pool) - 1] = '\0';
    }
    if (!p.laplace_variant.empty()) {
        strncpy(opts.laplace_variant, p.laplace_variant.c_str(), sizeof(opts.laplace_variant) - 1);
        opts.laplace_variant[sizeof(opts.laplace_variant) - 1] = '\0';
    }
    opts.laplace_seasonal_batch_init = p.laplace_seasonal_batch_init;
    opts.garch_p = static_cast<int>(p.garch_p);
    opts.garch_q = static_cast<int>(p.garch_q);
    if (!p.kalman_model.empty()) {
        strncpy(opts.kalman_model, p.kalman_model.c_str(), sizeof(opts.kalman_model) - 1);
        opts.kalman_model[sizeof(opts.kalman_model) - 1] = '\0';
    }
    // AutoEnsemble params (Phase 4)
    opts.ensemble_top_k = static_cast<int>(p.ensemble_top_k);
    if (!p.ensemble_method.empty()) {
        strncpy(opts.ensemble_method, p.ensemble_method.c_str(), sizeof(opts.ensemble_method) - 1);
        opts.ensemble_method[sizeof(opts.ensemble_method) - 1] = '\0';
    }
    // ARIMA order params (D-08/D-09, plan 10-02)
    if (!p.arima_order.empty()) {
        strncpy(opts.arima_order_str, p.arima_order.c_str(), sizeof(opts.arima_order_str) - 1);
        opts.arima_order_str[sizeof(opts.arima_order_str) - 1] = '\0';
    }
    if (!p.seasonal_order.empty()) {
        strncpy(opts.seasonal_order_str, p.seasonal_order.c_str(), sizeof(opts.seasonal_order_str) - 1);
        opts.seasonal_order_str[sizeof(opts.seasonal_order_str) - 1] = '\0';
    }
}

} // namespace duckdb
