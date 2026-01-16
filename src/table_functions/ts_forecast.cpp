#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

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
    return LogicalType::STRUCT(std::move(children));
}

static void ExtractListValues(Vector &list_vec, idx_t count, idx_t row_idx,
                              vector<double> &out_values,
                              vector<uint64_t> &out_validity) {
    // Use UnifiedVectorFormat to handle all vector types (flat, constant, dictionary)
    UnifiedVectorFormat list_data;
    list_vec.ToUnifiedFormat(count, list_data);

    auto list_entries = UnifiedVectorFormat::GetData<list_entry_t>(list_data);
    auto list_idx = list_data.sel->get_index(row_idx);
    auto &list_entry = list_entries[list_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);

    // Also use UnifiedVectorFormat for child vector
    UnifiedVectorFormat child_data;
    child_vec.ToUnifiedFormat(ListVector::GetListSize(list_vec), child_data);
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

static void SetListFromArray(Vector &result, idx_t field_idx, idx_t row_idx,
                             double *data, size_t length) {
    auto &children = StructVector::GetEntries(result);
    auto &list_vec = *children[field_idx];

    auto list_data = FlatVector::GetData<list_entry_t>(list_vec);
    auto &list_child = ListVector::GetEntry(list_vec);
    auto current_size = ListVector::GetListSize(list_vec);

    list_data[row_idx].offset = current_size;
    list_data[row_idx].length = length;

    ListVector::Reserve(list_vec, current_size + length);
    ListVector::SetListSize(list_vec, current_size + length);

    auto child_data = FlatVector::GetData<double>(list_child);
    if (data && length > 0) {
        memcpy(child_data + current_size, data, length * sizeof(double));
    }
}

template <typename T>
static void SetStructField(Vector &result, idx_t field_idx, idx_t row_idx, T value) {
    auto &children = StructVector::GetEntries(result);
    auto data = FlatVector::GetData<T>(*children[field_idx]);
    data[row_idx] = value;
}

static void SetStringField(Vector &result, idx_t field_idx, idx_t row_idx, const char *value) {
    auto &children = StructVector::GetEntries(result);
    auto data = FlatVector::GetData<string_t>(*children[field_idx]);
    data[row_idx] = StringVector::AddString(*children[field_idx], value);
}

static void TsForecastFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    auto &horizon_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Use UnifiedVectorFormat to handle both constant and flat vectors
    UnifiedVectorFormat list_format;
    list_vec.ToUnifiedFormat(count, list_format);

    UnifiedVectorFormat horizon_data;
    horizon_vec.ToUnifiedFormat(count, horizon_data);

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
        opts.auto_detect_seasonality = true;
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
        SetListFromArray(result, 3, row_idx, fcst_result.fitted_values, fcst_result.n_fitted);
        SetListFromArray(result, 4, row_idx, fcst_result.residuals, fcst_result.n_fitted);

        // Set scalar fields
        SetStringField(result, 5, row_idx, fcst_result.model_name);
        SetStructField<double>(result, 6, row_idx, fcst_result.aic);
        SetStructField<double>(result, 7, row_idx, fcst_result.bic);
        SetStructField<double>(result, 8, row_idx, fcst_result.mse);

        // Free Rust-allocated memory
        anofox_free_forecast_result(&fcst_result);
    }
}

// Helper to extract nested list (LIST<LIST<DOUBLE>>) into vector of vectors
static void ExtractNestedListValues(Vector &list_vec, idx_t count, idx_t row_idx,
                                    vector<vector<double>> &out_regressors) {
    out_regressors.clear();

    UnifiedVectorFormat list_data;
    list_vec.ToUnifiedFormat(count, list_data);

    auto list_idx = list_data.sel->get_index(row_idx);
    if (!list_data.validity.RowIsValid(list_idx)) {
        return;
    }

    auto list_entries = UnifiedVectorFormat::GetData<list_entry_t>(list_data);
    auto &outer_entry = list_entries[list_idx];

    auto &inner_list_vec = ListVector::GetEntry(list_vec);
    idx_t outer_size = outer_entry.length;
    idx_t outer_offset = outer_entry.offset;

    for (idx_t i = 0; i < outer_size; i++) {
        idx_t inner_idx = outer_offset + i;

        UnifiedVectorFormat inner_data;
        inner_list_vec.ToUnifiedFormat(ListVector::GetListSize(list_vec), inner_data);
        auto inner_entries = UnifiedVectorFormat::GetData<list_entry_t>(inner_data);

        auto inner_unified_idx = inner_data.sel->get_index(inner_idx);
        auto &inner_entry = inner_entries[inner_unified_idx];

        auto &values_vec = ListVector::GetEntry(inner_list_vec);
        UnifiedVectorFormat values_data;
        values_vec.ToUnifiedFormat(ListVector::GetListSize(inner_list_vec), values_data);
        auto values = UnifiedVectorFormat::GetData<double>(values_data);

        vector<double> regressor;
        for (idx_t j = 0; j < inner_entry.length; j++) {
            idx_t val_idx = inner_entry.offset + j;
            auto val_unified_idx = values_data.sel->get_index(val_idx);
            if (values_data.validity.RowIsValid(val_unified_idx)) {
                regressor.push_back(values[val_unified_idx]);
            } else {
                regressor.push_back(0.0);  // Fill NULL with 0 for regressors
            }
        }
        out_regressors.push_back(std::move(regressor));
    }
}

static void TsForecastExogFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];       // y values
    auto &xreg_vec = args.data[1];       // historical X regressors
    auto &future_xreg_vec = args.data[2]; // future X regressors
    auto &horizon_vec = args.data[3];
    auto &model_vec = args.data[4];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    UnifiedVectorFormat list_format;
    list_vec.ToUnifiedFormat(count, list_format);

    UnifiedVectorFormat horizon_data;
    horizon_vec.ToUnifiedFormat(count, horizon_data);

    UnifiedVectorFormat model_data;
    model_vec.ToUnifiedFormat(count, model_data);

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

        // Extract historical X regressors
        vector<vector<double>> xreg;
        ExtractNestedListValues(xreg_vec, count, row_idx, xreg);

        // Extract future X regressors
        vector<vector<double>> future_xreg;
        ExtractNestedListValues(future_xreg_vec, count, row_idx, future_xreg);

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

        // Build exogenous data structure
        vector<ExogenousRegressor> regressors;
        for (size_t i = 0; i < xreg.size() && i < future_xreg.size(); i++) {
            ExogenousRegressor reg;
            reg.values = xreg[i].data();
            reg.n_values = xreg[i].size();
            reg.future_values = future_xreg[i].data();
            reg.n_future = future_xreg[i].size();
            regressors.push_back(reg);
        }

        ExogenousData exog_data;
        exog_data.regressors = regressors.empty() ? nullptr : regressors.data();
        exog_data.n_regressors = regressors.size();

        // Build options
        ForecastOptionsExog opts;
        memset(&opts, 0, sizeof(opts));
        size_t model_len = std::min(model_name.size(), (size_t)31);
        memcpy(opts.model, model_name.c_str(), model_len);
        opts.model[model_len] = '\0';
        opts.horizon = horizon;
        opts.confidence_level = 0.95;
        opts.seasonal_period = 0;
        opts.auto_detect_seasonality = true;
        opts.include_fitted = true;
        opts.include_residuals = true;
        opts.exog = regressors.empty() ? nullptr : &exog_data;

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
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        SetListFromArray(result, 0, row_idx, fcst_result.point_forecasts, fcst_result.n_forecasts);
        SetListFromArray(result, 1, row_idx, fcst_result.lower_bounds, fcst_result.n_forecasts);
        SetListFromArray(result, 2, row_idx, fcst_result.upper_bounds, fcst_result.n_forecasts);
        SetListFromArray(result, 3, row_idx, fcst_result.fitted_values, fcst_result.n_fitted);
        SetListFromArray(result, 4, row_idx, fcst_result.residuals, fcst_result.n_fitted);
        SetStringField(result, 5, row_idx, fcst_result.model_name);
        SetStructField<double>(result, 6, row_idx, fcst_result.aic);
        SetStructField<double>(result, 7, row_idx, fcst_result.bic);
        SetStructField<double>(result, 8, row_idx, fcst_result.mse);

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
    list_vec.ToUnifiedFormat(count, list_format);

    UnifiedVectorFormat horizon_data;
    horizon_vec.ToUnifiedFormat(count, horizon_data);

    UnifiedVectorFormat model_data;
    model_vec.ToUnifiedFormat(count, model_data);

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
        opts.auto_detect_seasonality = true;
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
        SetListFromArray(result, 3, row_idx, fcst_result.fitted_values, fcst_result.n_fitted);
        SetListFromArray(result, 4, row_idx, fcst_result.residuals, fcst_result.n_fitted);
        SetStringField(result, 5, row_idx, fcst_result.model_name);
        SetStructField<double>(result, 6, row_idx, fcst_result.aic);
        SetStructField<double>(result, 7, row_idx, fcst_result.bic);
        SetStructField<double>(result, 8, row_idx, fcst_result.mse);

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
    ts_forecast_basic.stability = FunctionStability::VOLATILE;
    ts_forecast_set.AddFunction(ts_forecast_basic);

    // _ts_forecast(values, horizon, model)
    // model can include ETS spec in format "ETS:AAA" or "ETS:MNM" etc.
    ScalarFunction ts_forecast_with_model(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::INTEGER), LogicalType(LogicalTypeId::VARCHAR)},
        GetForecastResultType(),
        TsForecastWithModelFunction
    );
    ts_forecast_with_model.stability = FunctionStability::VOLATILE;
    ts_forecast_set.AddFunction(ts_forecast_with_model);

    // Mark as internal to hide from duckdb_functions() and deprioritize in autocomplete
    CreateScalarFunctionInfo forecast_info(ts_forecast_set);
    forecast_info.internal = true;
    loader.RegisterFunction(forecast_info);

    // Internal scalar function for forecasting with exogenous variables
    // _ts_forecast_exog(values, xreg, future_xreg, horizon, model)
    // - values: LIST<DOUBLE> - target variable y
    // - xreg: LIST<LIST<DOUBLE>> - historical X regressors [n_regressors][n_obs]
    // - future_xreg: LIST<LIST<DOUBLE>> - future X regressors [n_regressors][horizon]
    // - horizon: INTEGER - forecast horizon
    // - model: VARCHAR - model name (AutoARIMA, ARIMAX, ThetaX, MFLESX, etc.)
    ScalarFunction ts_forecast_exog_func(
        "_ts_forecast_exog",
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),           // values
         LogicalType::LIST(LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))),  // xreg
         LogicalType::LIST(LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))),  // future_xreg
         LogicalType(LogicalTypeId::INTEGER),                             // horizon
         LogicalType(LogicalTypeId::VARCHAR)},                            // model
        GetForecastResultType(),
        TsForecastExogFunction
    );
    ts_forecast_exog_func.stability = FunctionStability::VOLATILE;

    // Mark as internal
    CreateScalarFunctionInfo exog_info(ts_forecast_exog_func);
    exog_info.internal = true;
    loader.RegisterFunction(exog_info);
}

// ts_forecast_by is implemented as a table macro in ts_macros.cpp
void RegisterTsForecastByFunction(ExtensionLoader &loader) {
    // No-op: functionality provided by table macro
}

// Note: ts_forecast_agg is implemented in aggregate_functions/ts_forecast_agg.cpp

} // namespace duckdb
