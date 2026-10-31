#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/common/types/vector.hpp"
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace duckdb {

static LogicalType GetForecastResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("point", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("lower", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("upper", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("fitted", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("residuals", LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))));
    children.push_back(make_pair("model", LogicalType(LogicalTypeId::VARCHAR)));
    children.push_back(make_pair("aic", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("bic", LogicalType(LogicalTypeId::DOUBLE)));
    children.push_back(make_pair("mse", LogicalType(LogicalTypeId::DOUBLE)));
    // Appended (#280, CTR-06): interval coverage as a fraction; NULL without an interval.
    children.push_back(make_pair("level", LogicalType(LogicalTypeId::DOUBLE)));
    return LogicalType::STRUCT(std::move(children));
}

static void ExtractListValues(Vector &list_vec, idx_t count, idx_t row_idx,
                              vector<double> &out_values,
                              vector<uint64_t> &out_validity) {
    // Use UnifiedVectorFormat to handle all vector types (flat, constant, dictionary)
    UnifiedVectorFormat list_data;
    ToUnified(list_vec, count, list_data);

    auto list_entries = UnifiedVectorFormat::GetData<list_entry_t>(list_data);
    auto list_idx = list_data.sel->get_index(row_idx);
    auto &list_entry = list_entries[list_idx];

    auto &child_vec = ListChildMut(list_vec);

    // Also use UnifiedVectorFormat for child vector
    UnifiedVectorFormat child_data;
    ToUnified(child_vec, ListVector::GetListSize(list_vec), child_data);
    auto child_values = UnifiedVectorFormat::GetData<double>(child_data);

    out_values.clear();
    out_validity.clear();

    idx_t list_size = list_entry.length;
    idx_t list_offset = list_entry.offset;

    out_values.resize(list_size);
    size_t validity_words = (list_size + 63) / 64;
    out_validity.resize(validity_words, 0);

    for (idx_t i = 0; i < list_size; i++) {
        idx_t child_idx = list_offset + i;
        auto unified_child_idx = child_data.sel->get_index(child_idx);
        if (child_data.validity.RowIsValid(unified_child_idx)) {
            out_values[i] = child_values[unified_child_idx];
            out_validity[i / 64] |= (1ULL << (i % 64));
        } else {
            out_values[i] = 0.0;
        }
    }
}

// Set the row_idx'th element of a LIST(DOUBLE) struct field from a raw
// double* + length (T-10-13). A null `data` pointer (a model with no
// native interval/fitted/residual support, e.g. GARCH's lower/upper, or a
// length-mismatch guard upstream) makes the FIELD itself SQL NULL for this
// row — regardless of `length` — never a length>0 list backed by
// unreserved/uninitialised memory, which the old `if (data && length > 0)`
// guard allowed by still recording `length` in the list entry while
// skipping the memcpy.
static void SetListFromArray(Vector &result, idx_t field_idx, idx_t row_idx,
                             double *data, size_t length) {
    auto &children = StructVector::GetEntries(result);
    auto &list_vec = StructEntry(children[field_idx]);
    auto list_data = FlatDataMut<list_entry_t>(list_vec);
    auto current_size = ListVector::GetListSize(list_vec);

    if (!data) {
        FlatVector::SetNull(list_vec, row_idx, true);
        list_data[row_idx].offset = current_size;
        list_data[row_idx].length = 0;
        return;
    }

    auto &list_child = ListChildMut(list_vec);

    list_data[row_idx].offset = current_size;
    list_data[row_idx].length = length;

    ListVector::Reserve(list_vec, current_size + length);
    ListVector::SetListSize(list_vec, current_size + length);

    auto child_data = FlatDataMut<double>(list_child);
    if (length > 0) {
        memcpy(child_data + current_size, data, length * sizeof(double));
    }
}

// Same null-pointer-to-SQL-NULL-field policy as `SetListFromArray`, but ALSO
// marks each individual NaN element as an invalid (SQL NULL) entry in the
// list child's validity mask instead of writing through the NaN bit
// pattern — used for `fitted`/`residuals`, which carry per-position NaN at
// interpolated/trimmed positions (D-11/D-12) that must surface as SQL NULL
// elements inside an otherwise non-NULL list, not as the floating-point NaN
// value itself.
static void SetListFromArrayNanSafe(Vector &result, idx_t field_idx, idx_t row_idx,
                                    double *data, size_t length) {
    auto &children = StructVector::GetEntries(result);
    auto &list_vec = StructEntry(children[field_idx]);
    auto list_data = FlatDataMut<list_entry_t>(list_vec);
    auto current_size = ListVector::GetListSize(list_vec);

    if (!data) {
        FlatVector::SetNull(list_vec, row_idx, true);
        list_data[row_idx].offset = current_size;
        list_data[row_idx].length = 0;
        return;
    }

    auto &list_child = ListChildMut(list_vec);

    list_data[row_idx].offset = current_size;
    list_data[row_idx].length = length;

    ListVector::Reserve(list_vec, current_size + length);
    ListVector::SetListSize(list_vec, current_size + length);

    auto child_data = FlatDataMut<double>(list_child);
    for (size_t i = 0; i < length; i++) {
        if (std::isnan(data[i])) {
            FlatVector::SetNull(list_child, current_size + i, true);
            child_data[current_size + i] = 0.0;
        } else {
            child_data[current_size + i] = data[i];
        }
    }
}

template <typename T>
static void SetStructField(Vector &result, idx_t field_idx, idx_t row_idx, T value) {
    auto &children = StructVector::GetEntries(result);
    auto data = FlatDataMut<T>(StructEntry(children[field_idx]));
    data[row_idx] = value;
}

static void SetStringField(Vector &result, idx_t field_idx, idx_t row_idx, const char *value) {
    auto &children = StructVector::GetEntries(result);
    auto data = FlatDataMut<string_t>(StructEntry(children[field_idx]));
    data[row_idx] = StringVector::AddString(StructEntry(children[field_idx]), value);
}

// level field (#280): the confidence level the interval was computed at, or
// SQL NULL when the model returned no interval.
static void SetLevelField(Vector &result, idx_t field_idx, idx_t row_idx, const ForecastResult &fcst_result,
                          double confidence_level) {
    auto &children = StructVector::GetEntries(result);
    auto &level_vec = StructEntry(children[field_idx]);
    if (!fcst_result.lower_bounds && !fcst_result.upper_bounds) {
        FlatVector::SetNull(level_vec, row_idx, true);
        return;
    }
    FlatDataMut<double>(level_vec)[row_idx] = confidence_level;
}

static void TsForecastFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    auto &horizon_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Use UnifiedVectorFormat to handle both constant and flat vectors
    UnifiedVectorFormat list_format;
    ToUnified(list_vec, count, list_format);

    UnifiedVectorFormat horizon_data;
    ToUnified(horizon_vec, count, horizon_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto list_idx = list_format.sel->get_index(row_idx);
        if (!list_format.validity.RowIsValid(list_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        vector<uint64_t> validity;
        ExtractListValues(list_vec, count, row_idx, values, validity);

        // Get horizon from unified format (handles constant vectors correctly)
        auto horizon_idx = horizon_data.sel->get_index(row_idx);
        int32_t horizon = 12;
        if (horizon_data.validity.RowIsValid(horizon_idx)) {
            horizon = UnifiedVectorFormat::GetData<int32_t>(horizon_data)[horizon_idx];
        }

        ForecastOptions opts;
        memset(&opts, 0, sizeof(opts));
        memcpy(opts.model, "auto", 5);
        opts.horizon = horizon;
        opts.confidence_level = 0.95;
        opts.seasonal_period = 0;
        opts.auto_detect_seasonality = false;
        opts.include_fitted = true;
        opts.include_residuals = true;

        ForecastResult fcst_result;
        memset(&fcst_result, 0, sizeof(fcst_result));
        AnofoxError error;

        bool success = anofox_ts_forecast(
            values.data(),
            validity.empty() ? nullptr : validity.data(),
            values.size(),
            &opts,
            &fcst_result,
            &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Set list fields
        SetListFromArray(result, 0, row_idx, fcst_result.point_forecasts, fcst_result.n_forecasts);
        SetListFromArray(result, 1, row_idx, fcst_result.lower_bounds, fcst_result.n_forecasts);
        SetListFromArray(result, 2, row_idx, fcst_result.upper_bounds, fcst_result.n_forecasts);
        SetListFromArrayNanSafe(result, 3, row_idx, fcst_result.fitted_values, fcst_result.n_fitted);
        SetListFromArrayNanSafe(result, 4, row_idx, fcst_result.residuals, fcst_result.n_residuals);

        // Set scalar fields
        SetStringField(result, 5, row_idx, fcst_result.model_name);
        SetStructField<double>(result, 6, row_idx, fcst_result.aic);
        SetStructField<double>(result, 7, row_idx, fcst_result.bic);
        SetStructField<double>(result, 8, row_idx, fcst_result.mse);
        SetLevelField(result, 9, row_idx, fcst_result, opts.confidence_level);

        // Free Rust-allocated memory
        anofox_free_forecast_result(&fcst_result);
    }
}

// Helper to extract nested list (LIST<LIST<DOUBLE>>) into vector of vectors
// Extracts a LIST<LIST<DOUBLE>> cell into `out_regressors` (one Vec per
// regressor) AND a matching per-regressor validity bitmask in
// `out_validity` (bit set = valid; D-12/FCST-08). A NULL element keeps its
// slot at the documented placeholder `0.0` in `out_regressors`, but its bit
// is cleared in `out_validity` — the FFI/Rust boundary (not this 0.0) is
// what decides the value is missing, so no 0-fill semantics reach Rust.
static void ExtractNestedListValues(Vector &list_vec, idx_t count, idx_t row_idx,
                                    vector<vector<double>> &out_regressors,
                                    vector<vector<uint64_t>> &out_validity) {
    out_regressors.clear();
    out_validity.clear();

    UnifiedVectorFormat list_data;
    ToUnified(list_vec, count, list_data);

    auto list_idx = list_data.sel->get_index(row_idx);
    if (!list_data.validity.RowIsValid(list_idx)) {
        return;
    }

    auto list_entries = UnifiedVectorFormat::GetData<list_entry_t>(list_data);
    auto &outer_entry = list_entries[list_idx];

    auto &inner_list_vec = ListChildMut(list_vec);
    idx_t outer_size = outer_entry.length;
    idx_t outer_offset = outer_entry.offset;

    for (idx_t i = 0; i < outer_size; i++) {
        idx_t inner_idx = outer_offset + i;

        UnifiedVectorFormat inner_data;
        ToUnified(inner_list_vec, ListVector::GetListSize(list_vec), inner_data);
        auto inner_entries = UnifiedVectorFormat::GetData<list_entry_t>(inner_data);

        auto inner_unified_idx = inner_data.sel->get_index(inner_idx);
        auto &inner_entry = inner_entries[inner_unified_idx];

        auto &values_vec = ListChildMut(inner_list_vec);
        UnifiedVectorFormat values_data;
        ToUnified(values_vec, ListVector::GetListSize(inner_list_vec), values_data);
        auto values = UnifiedVectorFormat::GetData<double>(values_data);

        vector<double> regressor;
        regressor.reserve(inner_entry.length);
        size_t validity_words = (inner_entry.length + 63) / 64;
        vector<uint64_t> validity(validity_words, 0);

        for (idx_t j = 0; j < inner_entry.length; j++) {
            idx_t val_idx = inner_entry.offset + j;
            auto val_unified_idx = values_data.sel->get_index(val_idx);
            if (values_data.validity.RowIsValid(val_unified_idx)) {
                regressor.push_back(values[val_unified_idx]);
                validity[j / 64] |= (1ULL << (j % 64));
            } else {
                regressor.push_back(0.0);  // placeholder only; bit cleared below
            }
        }
        out_regressors.push_back(std::move(regressor));
        out_validity.push_back(std::move(validity));
    }
}

// ============================================================================
// _ts_forecast_exog parameter parsing (mirrors ts_forecast_scalar.cpp's
// ParseStringParam/ParseInt64Param/ParseDoubleParam/ValidateParams — kept as
// a separate local copy per this file's established pattern rather than a
// shared header, matching ts_forecast_native.cpp's own local copy).
// ============================================================================

static string ExogParseStringParam(const Value &params_value, const string &key, const string &default_val) {
    if (params_value.IsNull()) return default_val;
    if (params_value.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params_value);
        for (auto &child : map_children) {
            auto &k = StructValue::GetChildren(child)[0];
            auto &v = StructValue::GetChildren(child)[1];
            if (k.ToString() == key && !v.IsNull()) return v.ToString();
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

static int64_t ExogParseInt64Param(const Value &params_value, const string &key, int64_t default_val) {
    string str = ExogParseStringParam(params_value, key, "");
    if (str.empty()) return default_val;
    try { return std::stoll(str); } catch (...) { return default_val; }
}

static double ExogParseDoubleParam(const Value &params_value, const string &key, double default_val) {
    string str = ExogParseStringParam(params_value, key, "");
    if (str.empty()) return default_val;
    try { return std::stod(str); } catch (...) { return default_val; }
}

static void ExogValidateParamKeys(const Value &params_value) {
    static const unordered_set<string> valid_keys = {
        "model", "seasonal_period", "seasonal_periods", "confidence_level", "window", "model_pool",
        "laplace_variant", "laplace_seasonal_batch_init",
        "garch_p", "garch_q", "kalman_model",
        "top_k", "combination_method",
        "arima_order", "seasonal_order"
    };

    if (params_value.IsNull()) return;

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
            "Unknown parameter(s): %s. Valid parameters are: model, seasonal_period, seasonal_periods, confidence_level, window, model_pool, laplace_variant, laplace_seasonal_batch_init, garch_p, garch_q, kalman_model, top_k, combination_method, arima_order, seasonal_order",
            unknown_list);
    }
}

// strncpy-size-1 + terminator into a fixed C buffer (the pattern every
// ForecastOptions*-filling call site in this codebase uses for FFI string
// fields).
template <size_t N>
static void SetCField(char (&dst)[N], const string &s) {
    if (s.empty()) return;
    strncpy(dst, s.c_str(), N - 1);
    dst[N - 1] = '\0';
}

// `_ts_forecast_exog` — both the 5-argument (no params) and 6-argument
// (params ANY) overloads share this one callback; `args.ColumnCount()`
// tells us which overload bound (D-02/D-10/D-11/D-12 at the SQL layer).
static void TsForecastExogFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];       // y values
    auto &xreg_vec = args.data[1];       // historical X regressors
    auto &future_xreg_vec = args.data[2]; // future X regressors
    auto &horizon_vec = args.data[3];
    auto &model_vec = args.data[4];
    bool has_params = args.ColumnCount() >= 6;
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    UnifiedVectorFormat list_format;
    ToUnified(list_vec, count, list_format);

    UnifiedVectorFormat horizon_data;
    ToUnified(horizon_vec, count, horizon_data);

    UnifiedVectorFormat model_data;
    ToUnified(model_vec, count, model_data);

    UnifiedVectorFormat params_data;
    if (has_params) {
        ToUnified(args.data[5], count, params_data);
    }

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto list_idx = list_format.sel->get_index(row_idx);
        if (!list_format.validity.RowIsValid(list_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Extract y values
        vector<double> values;
        vector<uint64_t> validity;
        ExtractListValues(list_vec, count, row_idx, values, validity);

        // Extract historical/future X regressors + their per-element
        // validity bitmasks (D-12/FCST-08).
        vector<vector<double>> xreg;
        vector<vector<uint64_t>> xreg_validity;
        ExtractNestedListValues(xreg_vec, count, row_idx, xreg, xreg_validity);

        vector<vector<double>> future_xreg;
        vector<vector<uint64_t>> future_xreg_validity;
        ExtractNestedListValues(future_xreg_vec, count, row_idx, future_xreg, future_xreg_validity);

        // Historical/future regressor COUNT mismatch is now a hard error
        // (T-10-14) — the old `for (...; i < xreg.size() && i < future_xreg.size(); ...)`
        // loop silently truncated to the shorter list instead.
        if (xreg.size() != future_xreg.size()) {
            throw InvalidInputException(
                "_ts_forecast_exog: %lld historical regressor column(s) but %lld future "
                "regressor column(s) — these must match",
                (int64_t)xreg.size(), (int64_t)future_xreg.size());
        }

        // Get horizon
        auto horizon_idx = horizon_data.sel->get_index(row_idx);
        int32_t horizon = 12;
        if (horizon_data.validity.RowIsValid(horizon_idx)) {
            horizon = UnifiedVectorFormat::GetData<int32_t>(horizon_data)[horizon_idx];
        }

        // Get model
        auto model_idx = model_data.sel->get_index(row_idx);
        string model_name = "AutoARIMA";
        if (model_data.validity.RowIsValid(model_idx)) {
            model_name = UnifiedVectorFormat::GetData<string_t>(model_data)[model_idx].GetString();
        }

        // Defaults — confidence_level 0.90 now matches ts_forecast_by
        // (was a hard-coded 0.95; CHANGELOG item, 10-06).
        string model_spec;
        int64_t seasonal_period = 0;
        double confidence_level = 0.90;
        int64_t window_param = 0;
        string seasonal_periods_str;
        string model_pool;
        string laplace_variant;
        bool laplace_seasonal_batch_init = false;
        int64_t garch_p = 0;
        int64_t garch_q = 0;
        string kalman_model;
        int64_t ensemble_top_k = 0;
        string ensemble_method;
        string arima_order;
        string seasonal_order;

        if (has_params) {
            auto p_idx = params_data.sel->get_index(row_idx);
            if (params_data.validity.RowIsValid(p_idx)) {
                Value params_val = NormalizeParamsValue(args.data[5].GetValue(row_idx));
                ExogValidateParamKeys(params_val);
                model_spec = ExogParseStringParam(params_val, "model", "");
                seasonal_period = ExogParseInt64Param(params_val, "seasonal_period", 0);
                confidence_level = ExogParseDoubleParam(params_val, "confidence_level", 0.90);
                window_param = ExogParseInt64Param(params_val, "window", 0);
                seasonal_periods_str = ExogParseStringParam(params_val, "seasonal_periods", "");
                model_pool = ExogParseStringParam(params_val, "model_pool", "");
                laplace_variant = ExogParseStringParam(params_val, "laplace_variant", "");
                laplace_seasonal_batch_init =
                    ExogParseInt64Param(params_val, "laplace_seasonal_batch_init", 0) != 0;
                garch_p = ExogParseInt64Param(params_val, "garch_p", 0);
                garch_q = ExogParseInt64Param(params_val, "garch_q", 0);
                kalman_model = ExogParseStringParam(params_val, "kalman_model", "");
                ensemble_top_k = ExogParseInt64Param(params_val, "top_k", 0);
                ensemble_method = ExogParseStringParam(params_val, "combination_method", "");
                arima_order = ExogParseStringParam(params_val, "arima_order", "");
                seasonal_order = ExogParseStringParam(params_val, "seasonal_order", "");
            }
        }

        // WR-03: reject arima_order/seasonal_order strings that would
        // silently truncate when SetCField copies them into the fixed
        // 32-byte ForecastOptionsExog::arima_order_str/seasonal_order_str
        // buffers below.
        if (arima_order.size() > sizeof(ForecastOptionsExog::arima_order_str) - 1) {
            throw InvalidInputException(
                "arima_order value is too long (max %d characters): '%s'",
                (int)(sizeof(ForecastOptionsExog::arima_order_str) - 1), arima_order);
        }
        if (seasonal_order.size() > sizeof(ForecastOptionsExog::seasonal_order_str) - 1) {
            throw InvalidInputException(
                "seasonal_order value is too long (max %d characters): '%s'",
                (int)(sizeof(ForecastOptionsExog::seasonal_order_str) - 1), seasonal_order);
        }

        // Build exogenous data structure — validity/future_validity
        // pointers (T-10-12) stay alive through the FFI call: they point
        // into `xreg_validity`/`future_xreg_validity`, which outlive this
        // scope's `anofox_ts_forecast_exog` call (same scope).
        vector<ExogenousRegressor> regressors(xreg.size());
        for (size_t i = 0; i < xreg.size(); i++) {
            memset(&regressors[i], 0, sizeof(ExogenousRegressor));
            regressors[i].values = xreg[i].data();
            regressors[i].n_values = xreg[i].size();
            regressors[i].future_values = future_xreg[i].data();
            regressors[i].n_future = future_xreg[i].size();
            regressors[i].validity = xreg_validity[i].data();
            regressors[i].future_validity = future_xreg_validity[i].data();
        }

        ExogenousData exog_data;
        exog_data.regressors = regressors.empty() ? nullptr : regressors.data();
        exog_data.n_regressors = regressors.size();

        // Build options
        ForecastOptionsExog opts;
        memset(&opts, 0, sizeof(opts));
        SetCField(opts.model, model_name);
        SetCField(opts.ets_model, model_spec);
        opts.horizon = horizon;
        opts.confidence_level = confidence_level;
        opts.seasonal_period = static_cast<int>(seasonal_period);
        opts.auto_detect_seasonality = (seasonal_period == 0 && seasonal_periods_str.empty());
        opts.include_fitted = true;
        opts.include_residuals = true;
        opts.exog = regressors.empty() ? nullptr : &exog_data;
        opts.window = static_cast<int>(window_param);
        SetCField(opts.seasonal_periods_str, seasonal_periods_str);
        SetCField(opts.model_pool, model_pool);
        SetCField(opts.laplace_variant, laplace_variant);
        opts.laplace_seasonal_batch_init = laplace_seasonal_batch_init;
        opts.garch_p = static_cast<int>(garch_p);
        opts.garch_q = static_cast<int>(garch_q);
        SetCField(opts.kalman_model, kalman_model);
        opts.ensemble_top_k = static_cast<int>(ensemble_top_k);
        SetCField(opts.ensemble_method, ensemble_method);
        SetCField(opts.arima_order_str, arima_order);
        SetCField(opts.seasonal_order_str, seasonal_order);

        ForecastResult fcst_result;
        memset(&fcst_result, 0, sizeof(fcst_result));
        AnofoxError error;

        bool success = anofox_ts_forecast_exog(
            values.data(),
            validity.empty() ? nullptr : validity.data(),
            values.size(),
            &opts,
            &fcst_result,
            &error
        );

        if (!success) {
            // Invalid input / invalid model — a parameter or regressor
            // problem the caller must fix — is a hard SQL error, not a
            // per-row NULL (mirrors ts_forecast_scalar.cpp/
            // ts_forecast_native.cpp's existing policy). Other error codes
            // (e.g. a genuinely insufficient series) keep per-row NULL
            // isolation.
            if (error.code == INVALID_INPUT || error.code == INVALID_MODEL) {
                throw InvalidInputException(string(error.message));
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        SetListFromArray(result, 0, row_idx, fcst_result.point_forecasts, fcst_result.n_forecasts);
        SetListFromArray(result, 1, row_idx, fcst_result.lower_bounds, fcst_result.n_forecasts);
        SetListFromArray(result, 2, row_idx, fcst_result.upper_bounds, fcst_result.n_forecasts);
        SetListFromArrayNanSafe(result, 3, row_idx, fcst_result.fitted_values, fcst_result.n_fitted);
        SetListFromArrayNanSafe(result, 4, row_idx, fcst_result.residuals, fcst_result.n_residuals);
        SetStringField(result, 5, row_idx, fcst_result.model_name);
        SetStructField<double>(result, 6, row_idx, fcst_result.aic);
        SetStructField<double>(result, 7, row_idx, fcst_result.bic);
        SetStructField<double>(result, 8, row_idx, fcst_result.mse);
        SetLevelField(result, 9, row_idx, fcst_result, opts.confidence_level);

        anofox_free_forecast_result(&fcst_result);
    }
}

static void TsForecastWithModelFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    auto &horizon_vec = args.data[1];
    auto &model_vec = args.data[2];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Use UnifiedVectorFormat to handle both constant and flat vectors
    UnifiedVectorFormat list_format;
    ToUnified(list_vec, count, list_format);

    UnifiedVectorFormat horizon_data;
    ToUnified(horizon_vec, count, horizon_data);

    UnifiedVectorFormat model_data;
    ToUnified(model_vec, count, model_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto list_idx = list_format.sel->get_index(row_idx);
        if (!list_format.validity.RowIsValid(list_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        vector<uint64_t> validity;
        ExtractListValues(list_vec, count, row_idx, values, validity);

        // Get horizon from unified format (handles constant vectors correctly)
        auto horizon_idx = horizon_data.sel->get_index(row_idx);
        int32_t horizon = 12;
        if (horizon_data.validity.RowIsValid(horizon_idx)) {
            horizon = UnifiedVectorFormat::GetData<int32_t>(horizon_data)[horizon_idx];
        }

        // Get model from unified format (handles constant vectors correctly)
        // Model can be in format "ETS" or "ETS:AAA" where AAA is the ETS spec
        auto model_idx = model_data.sel->get_index(row_idx);
        string model_name = "auto";
        string ets_spec = "";
        if (model_data.validity.RowIsValid(model_idx)) {
            string full_model = UnifiedVectorFormat::GetData<string_t>(model_data)[model_idx].GetString();
            // Check for ETS spec in format "ETS:AAA"
            auto colon_pos = full_model.find(':');
            if (colon_pos != string::npos) {
                model_name = full_model.substr(0, colon_pos);
                ets_spec = full_model.substr(colon_pos + 1);
            } else {
                model_name = full_model;
            }
        }

        ForecastOptions opts;
        memset(&opts, 0, sizeof(opts));
        size_t model_len = std::min(model_name.size(), (size_t)31);
        memcpy(opts.model, model_name.c_str(), model_len);
        opts.model[model_len] = '\0';

        // Set ETS spec if provided
        if (!ets_spec.empty()) {
            size_t ets_len = std::min(ets_spec.size(), (size_t)7);
            memcpy(opts.ets_model, ets_spec.c_str(), ets_len);
            opts.ets_model[ets_len] = '\0';
        }

        opts.horizon = horizon;
        opts.confidence_level = 0.95;
        opts.seasonal_period = 0;
        opts.auto_detect_seasonality = false;
        opts.include_fitted = true;
        opts.include_residuals = true;

        ForecastResult fcst_result;
        memset(&fcst_result, 0, sizeof(fcst_result));
        AnofoxError error;

        bool success = anofox_ts_forecast(
            values.data(),
            validity.empty() ? nullptr : validity.data(),
            values.size(),
            &opts,
            &fcst_result,
            &error
        );

        if (!success) {
            // For invalid input errors (including invalid ETS spec), throw exception
            if (error.code == INVALID_INPUT || error.code == INVALID_MODEL) {
                throw InvalidInputException(error.message);
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        SetListFromArray(result, 0, row_idx, fcst_result.point_forecasts, fcst_result.n_forecasts);
        SetListFromArray(result, 1, row_idx, fcst_result.lower_bounds, fcst_result.n_forecasts);
        SetListFromArray(result, 2, row_idx, fcst_result.upper_bounds, fcst_result.n_forecasts);
        SetListFromArrayNanSafe(result, 3, row_idx, fcst_result.fitted_values, fcst_result.n_fitted);
        SetListFromArrayNanSafe(result, 4, row_idx, fcst_result.residuals, fcst_result.n_residuals);
        SetStringField(result, 5, row_idx, fcst_result.model_name);
        SetStructField<double>(result, 6, row_idx, fcst_result.aic);
        SetStructField<double>(result, 7, row_idx, fcst_result.bic);
        SetStructField<double>(result, 8, row_idx, fcst_result.mse);
        SetLevelField(result, 9, row_idx, fcst_result, opts.confidence_level);

        anofox_free_forecast_result(&fcst_result);
    }
}

void RegisterTsForecastFunction(ExtensionLoader &loader) {
    // Internal scalar function used by ts_forecast and ts_forecast_by table macros
    // Named with underscore prefix to match C++ API (ts_forecast is table macro only)
    ScalarFunctionSet ts_forecast_set("_ts_forecast");

    // _ts_forecast(values, horizon)
    // Mark as VOLATILE to prevent constant folding (forecasting is expensive and shouldn't be folded)
    ScalarFunction ts_forecast_basic(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::INTEGER)},
        GetForecastResultType(),
        TsForecastFunction
    );
    MarkFallible(ts_forecast_basic);
    SetStabilityOf(ts_forecast_basic, FunctionStability::VOLATILE);
    ts_forecast_set.AddFunction(ts_forecast_basic);

    // _ts_forecast(values, horizon, model)
    // model can include ETS spec in format "ETS:AAA" or "ETS:MNM" etc.
    ScalarFunction ts_forecast_with_model(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::INTEGER), LogicalType(LogicalTypeId::VARCHAR)},
        GetForecastResultType(),
        TsForecastWithModelFunction
    );
    MarkFallible(ts_forecast_with_model);
    SetStabilityOf(ts_forecast_with_model, FunctionStability::VOLATILE);
    ts_forecast_set.AddFunction(ts_forecast_with_model);

    // Mark as internal to hide from duckdb_functions() and deprioritize in autocomplete
    CreateScalarFunctionInfo forecast_info(ts_forecast_set);
    forecast_info.internal = true;
    loader.RegisterFunction(forecast_info);

    // Internal scalar function set for forecasting with exogenous variables.
    // - values: LIST<DOUBLE> - target variable y
    // - xreg: LIST<LIST<DOUBLE>> - historical X regressors [n_regressors][n_obs]
    // - future_xreg: LIST<LIST<DOUBLE>> - future X regressors [n_regressors][horizon]
    // - horizon: INTEGER - forecast horizon
    // - model: VARCHAR - model name (AutoARIMA, OptimizedTheta, ETS, MFLES, MSTL, ...)
    // - params (6-arg overload only): MAP/STRUCT — same key set as
    //   ts_forecast_by's params (model/ets_spec, seasonal_period,
    //   seasonal_periods, confidence_level, window, model_pool,
    //   laplace_variant, laplace_seasonal_batch_init, garch_p, garch_q,
    //   kalman_model, top_k, combination_method, arima_order,
    //   seasonal_order).
    ScalarFunctionSet ts_forecast_exog_set("_ts_forecast_exog");

    ScalarFunction ts_forecast_exog_5arg(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),           // values
         LogicalType::LIST(LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))),  // xreg
         LogicalType::LIST(LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))),  // future_xreg
         LogicalType(LogicalTypeId::INTEGER),                             // horizon
         LogicalType(LogicalTypeId::VARCHAR)},                            // model
        GetForecastResultType(),
        TsForecastExogFunction
    );
    MarkFallible(ts_forecast_exog_5arg);
    SetStabilityOf(ts_forecast_exog_5arg, FunctionStability::VOLATILE);
    ts_forecast_exog_set.AddFunction(ts_forecast_exog_5arg);

    ScalarFunction ts_forecast_exog_6arg(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),           // values
         LogicalType::LIST(LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))),  // xreg
         LogicalType::LIST(LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))),  // future_xreg
         LogicalType(LogicalTypeId::INTEGER),                             // horizon
         LogicalType(LogicalTypeId::VARCHAR),                             // model
         LogicalType::ANY},                                               // params (MAP/STRUCT)
        GetForecastResultType(),
        TsForecastExogFunction
    );
    MarkFallible(ts_forecast_exog_6arg);
    SetStabilityOf(ts_forecast_exog_6arg, FunctionStability::VOLATILE);
    ts_forecast_exog_set.AddFunction(ts_forecast_exog_6arg);

    // Mark as internal
    CreateScalarFunctionInfo exog_info(ts_forecast_exog_set);
    exog_info.internal = true;
    loader.RegisterFunction(exog_info);
}

// ts_forecast_by is implemented as a table macro in ts_macros.cpp
void RegisterTsForecastByFunction(ExtensionLoader &loader) {
    // No-op: functionality provided by table macro
}

// Note: ts_forecast_agg is implemented in aggregate_functions/ts_forecast_agg.cpp

} // namespace duckdb
