#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

static LogicalType GetForecastResultType() {
    child_list_t<LogicalType> children;
    children.push_back(make_pair("point", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("lower", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("upper", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("fitted", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("residuals", LogicalType::LIST(LogicalType::DOUBLE)));
    children.push_back(make_pair("model", LogicalType::VARCHAR));
    children.push_back(make_pair("aic", LogicalType::DOUBLE));
    children.push_back(make_pair("bic", LogicalType::DOUBLE));
    children.push_back(make_pair("mse", LogicalType::DOUBLE));
    return LogicalType::STRUCT(std::move(children));
}

static void ExtractListValues(Vector &list_vec, idx_t row_idx,
                              vector<double> &out_values,
                              vector<uint64_t> &out_validity) {
    auto list_data = ListVector::GetData(list_vec);
    auto &list_entry = list_data[row_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);
    auto child_data = FlatVector::GetData<double>(child_vec);
    auto &child_validity = FlatVector::Validity(child_vec);

    out_values.clear();
    out_validity.clear();

    idx_t list_size = list_entry.length;
    idx_t list_offset = list_entry.offset;

    out_values.resize(list_size);
    size_t validity_words = (list_size + 63) / 64;
    out_validity.resize(validity_words, 0);

    for (idx_t i = 0; i < list_size; i++) {
        idx_t child_idx = list_offset + i;
        if (child_validity.RowIsValid(child_idx)) {
            out_values[i] = child_data[child_idx];
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
    UnifiedVectorFormat horizon_data;
    horizon_vec.ToUnifiedFormat(count, horizon_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        vector<uint64_t> validity;
        ExtractListValues(list_vec, row_idx, values, validity);

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
            throw InvalidInputException("ts_forecast failed: %s", error.message);
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

static void TsForecastWithModelFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    auto &horizon_vec = args.data[1];
    auto &model_vec = args.data[2];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Use UnifiedVectorFormat to handle both constant and flat vectors
    UnifiedVectorFormat horizon_data;
    horizon_vec.ToUnifiedFormat(count, horizon_data);

    UnifiedVectorFormat model_data;
    model_vec.ToUnifiedFormat(count, model_data);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        vector<uint64_t> validity;
        ExtractListValues(list_vec, row_idx, values, validity);

        // Get horizon from unified format (handles constant vectors correctly)
        auto horizon_idx = horizon_data.sel->get_index(row_idx);
        int32_t horizon = 12;
        if (horizon_data.validity.RowIsValid(horizon_idx)) {
            horizon = UnifiedVectorFormat::GetData<int32_t>(horizon_data)[horizon_idx];
        }

        // Get model from unified format (handles constant vectors correctly)
        auto model_idx = model_data.sel->get_index(row_idx);
        string model_name = "auto";
        if (model_data.validity.RowIsValid(model_idx)) {
            model_name = UnifiedVectorFormat::GetData<string_t>(model_data)[model_idx].GetString();
        }

        ForecastOptions opts;
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
            throw InvalidInputException("ts_forecast failed: %s", error.message);
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
    ScalarFunctionSet ts_forecast_set("ts_forecast");

    // ts_forecast(values, horizon)
    ts_forecast_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER},
        GetForecastResultType(),
        TsForecastFunction
    ));

    // ts_forecast(values, horizon, model)
    ts_forecast_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER, LogicalType::VARCHAR},
        GetForecastResultType(),
        TsForecastWithModelFunction
    ));

    loader.RegisterFunction(ts_forecast_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_forecast");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER},
        GetForecastResultType(),
        TsForecastFunction
    ));
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::INTEGER, LogicalType::VARCHAR},
        GetForecastResultType(),
        TsForecastWithModelFunction
    ));
    loader.RegisterFunction(anofox_set);
}

// ts_forecast_by is implemented as a table macro in ts_macros.cpp
void RegisterTsForecastByFunction(ExtensionLoader &loader) {
    // No-op: functionality provided by table macro
}

// Note: ts_forecast_agg is implemented in aggregate_functions/ts_forecast_agg.cpp

} // namespace duckdb
