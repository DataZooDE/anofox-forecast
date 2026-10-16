#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "ts_list_marshal.hpp"

#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

namespace duckdb {

// ============================================================================
// ts_mae - Mean Absolute Error
// ============================================================================

static void TsMaeFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &forecast_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader actual_reader(actual_vec, count);
    ListDoubleReader forecast_reader(forecast_vec, count);
    vector<double> actual, forecast;
    vector<uint64_t> actual_validity, forecast_validity;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actual_reader.IsNull(row_idx) || forecast_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        actual_reader.Read(row_idx, actual, actual_validity);
        forecast_reader.Read(row_idx, forecast, forecast_validity);

        AnofoxError error;
        double mae_result;
        bool success = anofox_ts_mae(
            actual.data(), actual_validity.empty() ? nullptr : actual_validity.data(), actual.size(),
            forecast.data(), forecast_validity.empty() ? nullptr : forecast_validity.data(), forecast.size(),
            &mae_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = mae_result;
    }
}

void RegisterTsMaeFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_mae_set("ts_mae");
    ts_mae_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMaeFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_mae_set);
        FunctionDescription desc;
        desc.description = "Computes Mean Absolute Error (MAE) between actual and predicted value arrays.";
        desc.examples = {"ts_mae(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mae");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMaeFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_mae";
        FunctionDescription desc;
        desc.description = "Computes Mean Absolute Error (MAE) between actual and predicted value arrays.";
        desc.examples = {"ts_mae(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_mse - Mean Squared Error
// ============================================================================

static void TsMseFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &forecast_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader actual_reader(actual_vec, count);
    ListDoubleReader forecast_reader(forecast_vec, count);
    vector<double> actual, forecast;
    vector<uint64_t> actual_validity, forecast_validity;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actual_reader.IsNull(row_idx) || forecast_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        actual_reader.Read(row_idx, actual, actual_validity);
        forecast_reader.Read(row_idx, forecast, forecast_validity);

        AnofoxError error;
        double mse_result;
        bool success = anofox_ts_mse(
            actual.data(), actual_validity.empty() ? nullptr : actual_validity.data(), actual.size(),
            forecast.data(), forecast_validity.empty() ? nullptr : forecast_validity.data(), forecast.size(),
            &mse_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = mse_result;
    }
}

void RegisterTsMseFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_mse_set("ts_mse");
    ts_mse_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMseFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_mse_set);
        FunctionDescription desc;
        desc.description = "Computes Mean Squared Error (MSE) between actual and predicted value arrays.";
        desc.examples = {"ts_mse(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mse");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMseFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_mse";
        FunctionDescription desc;
        desc.description = "Computes Mean Squared Error (MSE) between actual and predicted value arrays.";
        desc.examples = {"ts_mse(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_rmse - Root Mean Squared Error
// ============================================================================

static void TsRmseFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &forecast_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader actual_reader(actual_vec, count);
    ListDoubleReader forecast_reader(forecast_vec, count);
    vector<double> actual, forecast;
    vector<uint64_t> actual_validity, forecast_validity;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actual_reader.IsNull(row_idx) || forecast_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        actual_reader.Read(row_idx, actual, actual_validity);
        forecast_reader.Read(row_idx, forecast, forecast_validity);

        AnofoxError error;
        double rmse_result;
        bool success = anofox_ts_rmse(
            actual.data(), actual_validity.empty() ? nullptr : actual_validity.data(), actual.size(),
            forecast.data(), forecast_validity.empty() ? nullptr : forecast_validity.data(), forecast.size(),
            &rmse_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = rmse_result;
    }
}

void RegisterTsRmseFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_rmse_set("ts_rmse");
    ts_rmse_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsRmseFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_rmse_set);
        FunctionDescription desc;
        desc.description = "Computes Root Mean Squared Error (RMSE) between actual and predicted value arrays.";
        desc.examples = {"ts_rmse(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_rmse");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsRmseFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_rmse";
        FunctionDescription desc;
        desc.description = "Computes Root Mean Squared Error (RMSE) between actual and predicted value arrays.";
        desc.examples = {"ts_rmse(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_mape - Mean Absolute Percentage Error
// ============================================================================

static void TsMapeFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &forecast_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader actual_reader(actual_vec, count);
    ListDoubleReader forecast_reader(forecast_vec, count);
    vector<double> actual, forecast;
    vector<uint64_t> actual_validity, forecast_validity;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actual_reader.IsNull(row_idx) || forecast_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        actual_reader.Read(row_idx, actual, actual_validity);
        forecast_reader.Read(row_idx, forecast, forecast_validity);

        AnofoxError error;
        double mape_result;
        bool is_null = false;
        bool success = anofox_ts_mape(
            actual.data(), actual_validity.empty() ? nullptr : actual_validity.data(), actual.size(),
            forecast.data(), forecast_validity.empty() ? nullptr : forecast_validity.data(), forecast.size(),
            &mape_result, &is_null, &error
        );

        if (!success || is_null) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = mape_result;
    }
}

void RegisterTsMapeFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_mape_set("ts_mape");
    ts_mape_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMapeFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_mape_set);
        FunctionDescription desc;
        desc.description = "Computes Mean Absolute Percentage Error (MAPE) between actual and predicted value arrays. Returns NULL if any actual value is exactly zero (MAPE is undefined there); use ts_smape or ts_mase for intermittent-demand series.";
        desc.examples = {"ts_mape(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mape");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMapeFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_mape";
        FunctionDescription desc;
        desc.description = "Computes Mean Absolute Percentage Error (MAPE) between actual and predicted value arrays. Returns NULL if any actual value is exactly zero (MAPE is undefined there); use ts_smape or ts_mase for intermittent-demand series.";
        desc.examples = {"ts_mape(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_smape - Symmetric Mean Absolute Percentage Error
// ============================================================================

static void TsSmapeFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &forecast_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader actual_reader(actual_vec, count);
    ListDoubleReader forecast_reader(forecast_vec, count);
    vector<double> actual, forecast;
    vector<uint64_t> actual_validity, forecast_validity;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actual_reader.IsNull(row_idx) || forecast_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        actual_reader.Read(row_idx, actual, actual_validity);
        forecast_reader.Read(row_idx, forecast, forecast_validity);

        AnofoxError error;
        double smape_result;
        bool success = anofox_ts_smape(
            actual.data(), actual_validity.empty() ? nullptr : actual_validity.data(), actual.size(),
            forecast.data(), forecast_validity.empty() ? nullptr : forecast_validity.data(), forecast.size(),
            &smape_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = smape_result;
    }
}

void RegisterTsSmapeFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_smape_set("ts_smape");
    ts_smape_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsSmapeFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_smape_set);
        FunctionDescription desc;
        desc.description = "Computes Symmetric Mean Absolute Percentage Error (sMAPE) between actual and predicted value arrays.";
        desc.examples = {"ts_smape(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_smape");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsSmapeFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_smape";
        FunctionDescription desc;
        desc.description = "Computes Symmetric Mean Absolute Percentage Error (sMAPE) between actual and predicted value arrays.";
        desc.examples = {"ts_smape(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_mase - Mean Absolute Scaled Error (Hyndman & Koehler 2006, D-08)
// C++ API: ts_mase(actual[], forecast[], insample[], seasonal_period) → DOUBLE
//
// The pre-v2026.10.16 3-list form ts_mase(actual, forecast, baseline) computed a
// relative MAE, not MASE -- it is REMOVED (overload below throws naming
// ts_rmae, which still provides that computation).
// ============================================================================

static void TsMaseFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &forecast_vec = args.data[1];
    auto &insample_vec = args.data[2];
    auto &period_vec = args.data[3];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader actual_reader(actual_vec, count);
    ListDoubleReader forecast_reader(forecast_vec, count);
    ListDoubleReader insample_reader(insample_vec, count);
    ScalarArgReader<int64_t> period_reader(period_vec, count);
    vector<double> actual, forecast, insample;
    vector<uint64_t> actual_validity, forecast_validity, insample_validity;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actual_reader.IsNull(row_idx) || forecast_reader.IsNull(row_idx) ||
            insample_reader.IsNull(row_idx) || period_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        actual_reader.Read(row_idx, actual, actual_validity);
        forecast_reader.Read(row_idx, forecast, forecast_validity);
        insample_reader.Read(row_idx, insample, insample_validity);
        int64_t seasonal_period = period_reader.Get(row_idx);

        AnofoxError error;
        double mase_result;
        bool is_null = false;
        bool success = anofox_ts_mase(
            actual.data(), actual_validity.empty() ? nullptr : actual_validity.data(), actual.size(),
            forecast.data(), forecast_validity.empty() ? nullptr : forecast_validity.data(), forecast.size(),
            insample.data(), insample_validity.empty() ? nullptr : insample_validity.data(), insample.size(),
            seasonal_period,
            &mase_result, &is_null, &error
        );

        if (!success) {
            if (error.code == INVALID_INPUT) {
                throw InvalidInputException("ts_mase: %s", error.message);
            }
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        if (is_null) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = mase_result;
    }
}

// The removed 3-list form: throws at bind time naming the replacement.
static unique_ptr<FunctionData> TsMaseRemovedBind(ClientContext &, ScalarFunction &,
                                                   vector<unique_ptr<Expression>> &) {
    throw InvalidInputException(
        "ts_mase(actual, forecast, baseline) was removed in v2026.10.16: it computed a relative MAE, not MASE. "
        "Use ts_rmae(actual, forecast, baseline) for that result, or "
        "ts_mase(actual, forecast, insample, seasonal_period) for Hyndman-Koehler MASE.");
}

void RegisterTsMaseFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_mase_set("ts_mase");
    ts_mase_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT)},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMaseFunction
    ));
    ts_mase_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        ScalarFunction::NopFunction,
        TsMaseRemovedBind
    ));
    {
        CreateScalarFunctionInfo info(ts_mase_set);
        FunctionDescription desc;
        desc.description = "Computes Hyndman-Koehler Mean Absolute Scaled Error (MASE): the out-of-sample forecast MAE scaled by the in-sample seasonal-naive MAE at lag seasonal_period. The old 3-argument (actual, forecast, baseline) form was removed in v2026.10.16 (it computed a relative MAE, not MASE) -- use ts_rmae for that.";
        desc.examples = {"ts_mase(LIST(actual ORDER BY date), LIST(forecast ORDER BY date), LIST(insample ORDER BY date), 12)"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "forecast", "insample", "seasonal_period"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mase");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT)},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMaseFunction
    ));
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        ScalarFunction::NopFunction,
        TsMaseRemovedBind
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_mase";
        FunctionDescription desc;
        desc.description = "Computes Hyndman-Koehler Mean Absolute Scaled Error (MASE): the out-of-sample forecast MAE scaled by the in-sample seasonal-naive MAE at lag seasonal_period. The old 3-argument (actual, forecast, baseline) form was removed in v2026.10.16 (it computed a relative MAE, not MASE) -- use ts_rmae for that.";
        desc.examples = {"ts_mase(LIST(actual ORDER BY date), LIST(forecast ORDER BY date), LIST(insample ORDER BY date), 12)"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "forecast", "insample", "seasonal_period"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::BIGINT)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_r2 - R-squared (Coefficient of Determination)
// ============================================================================

static void TsR2Function(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &forecast_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader actual_reader(actual_vec, count);
    ListDoubleReader forecast_reader(forecast_vec, count);
    vector<double> actual, forecast;
    vector<uint64_t> actual_validity, forecast_validity;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actual_reader.IsNull(row_idx) || forecast_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        actual_reader.Read(row_idx, actual, actual_validity);
        forecast_reader.Read(row_idx, forecast, forecast_validity);

        AnofoxError error;
        double r2_result;
        bool success = anofox_ts_r2(
            actual.data(), actual_validity.empty() ? nullptr : actual_validity.data(), actual.size(),
            forecast.data(), forecast_validity.empty() ? nullptr : forecast_validity.data(), forecast.size(),
            &r2_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = r2_result;
    }
}

void RegisterTsR2Function(ExtensionLoader &loader) {
    ScalarFunctionSet ts_r2_set("ts_r2");
    ts_r2_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsR2Function
    ));
    {
        CreateScalarFunctionInfo info(ts_r2_set);
        FunctionDescription desc;
        desc.description = "Computes the R-squared (coefficient of determination) between actual and predicted value arrays.";
        desc.examples = {"ts_r2(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_r2");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsR2Function
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_r2";
        FunctionDescription desc;
        desc.description = "Computes the R-squared (coefficient of determination) between actual and predicted value arrays.";
        desc.examples = {"ts_r2(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_bias - Bias (Mean Error)
// ============================================================================

static void TsBiasFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &forecast_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader actual_reader(actual_vec, count);
    ListDoubleReader forecast_reader(forecast_vec, count);
    vector<double> actual, forecast;
    vector<uint64_t> actual_validity, forecast_validity;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actual_reader.IsNull(row_idx) || forecast_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        actual_reader.Read(row_idx, actual, actual_validity);
        forecast_reader.Read(row_idx, forecast, forecast_validity);

        AnofoxError error;
        double bias_result;
        bool success = anofox_ts_bias(
            actual.data(), actual_validity.empty() ? nullptr : actual_validity.data(), actual.size(),
            forecast.data(), forecast_validity.empty() ? nullptr : forecast_validity.data(), forecast.size(),
            &bias_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = bias_result;
    }
}

void RegisterTsBiasFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_bias_set("ts_bias");
    ts_bias_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsBiasFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_bias_set);
        FunctionDescription desc;
        desc.description = "Computes mean forecast bias (mean error) between actual and predicted value arrays.";
        desc.examples = {"ts_bias(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_bias");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsBiasFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_bias";
        FunctionDescription desc;
        desc.description = "Computes mean forecast bias (mean error) between actual and predicted value arrays.";
        desc.examples = {"ts_bias(LIST(actual ORDER BY date), LIST(forecast ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_rmae - Relative Mean Absolute Error
// C++ API: ts_rmae(actual[], pred1[], pred2[]) → DOUBLE
// Compares two model predictions: rMAE = MAE(actual, pred1) / MAE(actual, pred2)
// ============================================================================

static void TsRmaeFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &pred1_vec = args.data[1];
    auto &pred2_vec = args.data[2];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader actual_reader(actual_vec, count);
    ListDoubleReader pred1_reader(pred1_vec, count);
    ListDoubleReader pred2_reader(pred2_vec, count);
    vector<double> actual, pred1, pred2;
    vector<uint64_t> actual_validity, pred1_validity, pred2_validity;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actual_reader.IsNull(row_idx) || pred1_reader.IsNull(row_idx) ||
            pred2_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        actual_reader.Read(row_idx, actual, actual_validity);
        pred1_reader.Read(row_idx, pred1, pred1_validity);
        pred2_reader.Read(row_idx, pred2, pred2_validity);

        AnofoxError error;
        double rmae_result;
        bool success = anofox_ts_rmae(
            actual.data(), actual_validity.empty() ? nullptr : actual_validity.data(), actual.size(),
            pred1.data(), pred1_validity.empty() ? nullptr : pred1_validity.data(), pred1.size(),
            pred2.data(), pred2_validity.empty() ? nullptr : pred2_validity.data(), pred2.size(),
            &rmae_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = rmae_result;
    }
}

void RegisterTsRmaeFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_rmae_set("ts_rmae");
    ts_rmae_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsRmaeFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_rmae_set);
        FunctionDescription desc;
        desc.description = "Computes Relative MAE: ratio of MAE(actual, pred1) to MAE(actual, pred2). Values < 1 mean pred1 is better.";
        desc.examples = {"ts_rmae(LIST(actual ORDER BY date), LIST(model_a ORDER BY date), LIST(model_b ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "pred1", "pred2"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_rmae");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsRmaeFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_rmae";
        FunctionDescription desc;
        desc.description = "Computes Relative MAE: ratio of MAE(actual, pred1) to MAE(actual, pred2). Values < 1 mean pred1 is better.";
        desc.examples = {"ts_rmae(LIST(actual ORDER BY date), LIST(model_a ORDER BY date), LIST(model_b ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "pred1", "pred2"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_quantile_loss - Quantile Loss (Pinball Loss)
// ============================================================================

static void TsQuantileLossFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &forecast_vec = args.data[1];
    auto &quantile_vec = args.data[2];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    // Use UnifiedVectorFormat to handle both constant and flat vectors
    UnifiedVectorFormat quantile_data;
    quantile_vec.ToUnifiedFormat(count, quantile_data);

    ListDoubleReader actual_reader(actual_vec, count);
    ListDoubleReader forecast_reader(forecast_vec, count);
    vector<double> actual, forecast;
    vector<uint64_t> actual_validity, forecast_validity;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        // Get quantile from unified format first (handles constant vectors correctly)
        auto quantile_idx = quantile_data.sel->get_index(row_idx);
        if (actual_reader.IsNull(row_idx) || forecast_reader.IsNull(row_idx) ||
            !quantile_data.validity.RowIsValid(quantile_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        actual_reader.Read(row_idx, actual, actual_validity);
        forecast_reader.Read(row_idx, forecast, forecast_validity);

        double quantile = UnifiedVectorFormat::GetData<double>(quantile_data)[quantile_idx];

        AnofoxError error;
        double ql_result;
        bool success = anofox_ts_quantile_loss(
            actual.data(), actual_validity.empty() ? nullptr : actual_validity.data(), actual.size(),
            forecast.data(), forecast_validity.empty() ? nullptr : forecast_validity.data(), forecast.size(),
            quantile,
            &ql_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = ql_result;
    }
}

void RegisterTsQuantileLossFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_ql_set("ts_quantile_loss");
    ts_ql_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        LogicalType(LogicalTypeId::DOUBLE),
        TsQuantileLossFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_ql_set);
        FunctionDescription desc;
        desc.description = "Computes the quantile (pinball) loss for a single quantile level.";
        desc.examples = {"ts_quantile_loss(LIST(actual ORDER BY date), LIST(forecast ORDER BY date), 0.9)"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted", "quantile"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_quantile_loss");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)},
        LogicalType(LogicalTypeId::DOUBLE),
        TsQuantileLossFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_quantile_loss";
        FunctionDescription desc;
        desc.description = "Computes the quantile (pinball) loss for a single quantile level.";
        desc.examples = {"ts_quantile_loss(LIST(actual ORDER BY date), LIST(forecast ORDER BY date), 0.9)"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted", "quantile"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType(LogicalTypeId::DOUBLE)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_mqloss - Mean Quantile Loss
// C++ API: ts_mqloss(actual[], quantiles[][], levels[]) → DOUBLE
// ============================================================================

static void TsMqlossFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &quantiles_vec = args.data[1];  // LIST(LIST(DOUBLE)) - 2D array
    auto &levels_vec = args.data[2];      // LIST(DOUBLE) - quantile levels (parameter list)
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader actual_reader(actual_vec, count);
    NestedListDoubleReader quantiles_reader(quantiles_vec, count);
    ListDoubleReader levels_reader(levels_vec, count);
    vector<double> actual;
    vector<uint64_t> actual_validity;
    vector<vector<double>> quantiles;
    vector<vector<uint64_t>> quantiles_validity;
    vector<double> levels;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actual_reader.IsNull(row_idx) || quantiles_reader.IsNull(row_idx) ||
            levels_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        actual_reader.Read(row_idx, actual, actual_validity);
        quantiles_reader.Read(row_idx, quantiles, quantiles_validity);
        levels_reader.ReadNoNulls(row_idx, levels, "ts_mqloss", "levels");

        if (quantiles.size() != levels.size()) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // A NULL inner list (NestedListDoubleReader) comes back as an empty
        // values/validity vector; pad to actual.size() as all-invalid so the
        // FFI sees a length-matched, fully-missing level rather than a 0-length
        // array.
        vector<const double *> quantile_ptrs;
        vector<const uint64_t *> quantile_validity_ptrs;
        vector<vector<double>> padded_values;
        vector<vector<uint64_t>> padded_validity;
        padded_values.reserve(quantiles.size());
        padded_validity.reserve(quantiles.size());
        for (idx_t level = 0; level < quantiles.size(); level++) {
            if (quantiles[level].empty() && !actual.empty()) {
                padded_values.push_back(vector<double>(actual.size(), 0.0));
                padded_validity.push_back(vector<uint64_t>((actual.size() + 63) / 64, 0));
            } else {
                padded_values.push_back(quantiles[level]);
                padded_validity.push_back(quantiles_validity[level]);
            }
            quantile_ptrs.push_back(padded_values.back().data());
            quantile_validity_ptrs.push_back(
                padded_validity.back().empty() ? nullptr : padded_validity.back().data());
        }

        AnofoxError error;
        double mqloss_result;
        bool success = anofox_ts_mqloss(
            actual.data(), actual_validity.empty() ? nullptr : actual_validity.data(), actual.size(),
            quantile_ptrs.data(), quantile_validity_ptrs.data(), quantiles.size(),
            levels.data(),
            &mqloss_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = mqloss_result;
    }
}

void RegisterTsMqlossFunction(ExtensionLoader &loader) {
    // C++ API: ts_mqloss(actual[], quantiles[][], levels[])
    ScalarFunctionSet ts_mqloss_set("ts_mqloss");
    ts_mqloss_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMqlossFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_mqloss_set);
        FunctionDescription desc;
        desc.description = "Computes Mean Quantile Loss (MQLoss) across multiple quantile levels.";
        desc.examples = {"ts_mqloss(LIST(actual ORDER BY date), LIST([lower, upper] ORDER BY date), [0.1, 0.9])"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "quantiles", "levels"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mqloss");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)),
         LogicalType::LIST(LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))),
         LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMqlossFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_mqloss";
        FunctionDescription desc;
        desc.description = "Computes Mean Quantile Loss (MQLoss) across multiple quantile levels.";
        desc.examples = {"ts_mqloss(LIST(actual ORDER BY date), LIST([lower, upper] ORDER BY date), [0.1, 0.9])"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "quantiles", "levels"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_coverage - Coverage of Prediction Intervals
// ============================================================================

static void TsCoverageFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &lower_vec = args.data[1];
    auto &upper_vec = args.data[2];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    ListDoubleReader actual_reader(actual_vec, count);
    ListDoubleReader lower_reader(lower_vec, count);
    ListDoubleReader upper_reader(upper_vec, count);
    vector<double> actual, lower, upper;
    vector<uint64_t> actual_validity, lower_validity, upper_validity;

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (actual_reader.IsNull(row_idx) || lower_reader.IsNull(row_idx) ||
            upper_reader.IsNull(row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        actual_reader.Read(row_idx, actual, actual_validity);
        lower_reader.Read(row_idx, lower, lower_validity);
        upper_reader.Read(row_idx, upper, upper_validity);

        if (actual.size() != lower.size() || actual.size() != upper.size()) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        AnofoxError error;
        double cov_result;
        bool success = anofox_ts_coverage(
            actual.data(), actual_validity.empty() ? nullptr : actual_validity.data(),
            lower.data(), lower_validity.empty() ? nullptr : lower_validity.data(),
            upper.data(), upper_validity.empty() ? nullptr : upper_validity.data(),
            actual.size(),
            &cov_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = cov_result;
    }
}

void RegisterTsCoverageFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_coverage_set("ts_coverage");
    ts_coverage_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsCoverageFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_coverage_set);
        FunctionDescription desc;
        desc.description = "Computes the empirical coverage rate of prediction intervals: fraction of actuals within [lower, upper].";
        desc.examples = {"ts_coverage(LIST(actual ORDER BY date), LIST(lower ORDER BY date), LIST(upper ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "lower", "upper"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_coverage");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsCoverageFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_coverage";
        FunctionDescription desc;
        desc.description = "Computes the empirical coverage rate of prediction intervals: fraction of actuals within [lower, upper].";
        desc.examples = {"ts_coverage(LIST(actual ORDER BY date), LIST(lower ORDER BY date), LIST(upper ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "lower", "upper"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_estimate_backtest_memory - Estimate memory usage for backtest
// ============================================================================

static void TsEstimateBacktestMemoryFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &n_series_vec = args.data[0];
    auto &n_dates_vec = args.data[1];
    auto &folds_vec = args.data[2];
    auto &horizon_vec = args.data[3];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<int64_t>(result);

    auto n_series_data = FlatVector::GetData<int64_t>(n_series_vec);
    auto n_dates_data = FlatVector::GetData<int64_t>(n_dates_vec);
    auto folds_data = FlatVector::GetData<int64_t>(folds_vec);
    auto horizon_data = FlatVector::GetData<int64_t>(horizon_vec);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(n_series_vec, row_idx) ||
            FlatVector::IsNull(n_dates_vec, row_idx) ||
            FlatVector::IsNull(folds_vec, row_idx) ||
            FlatVector::IsNull(horizon_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        int64_t n_series = n_series_data[row_idx];
        int64_t n_dates = n_dates_data[row_idx];
        int64_t folds = folds_data[row_idx];
        int64_t horizon = horizon_data[row_idx];

        // Estimate memory usage for the ts_cv_folds_by + ts_cv_forecast_by two-step CV workflow:
        // 1. cv_splits (ts_cv_folds_by output): n_series × n_dates × folds × 40 bytes (5 columns × 8 bytes)
        // 2. cv_train/cv_test: same as cv_splits but filtered (~60% train, ~10% test)
        // 3. forecast LIST aggregation: n_series × folds × (n_dates/2) × 8 bytes
        // 4. output (ts_cv_forecast_by): n_series × folds × horizon × 88 bytes (11 columns × 8 bytes)
        //
        // Note: LIST() aggregates cannot spill to disk, so this is the main bottleneck

        int64_t cv_splits_bytes = n_series * n_dates * folds * 40;
        int64_t list_agg_bytes = n_series * folds * (n_dates / 2) * 8;
        int64_t output_bytes = n_series * folds * horizon * 88;

        // Total estimate (with some overhead factor)
        int64_t total_bytes = cv_splits_bytes + list_agg_bytes + output_bytes;
        int64_t estimated_mb = total_bytes / (1024 * 1024);

        result_data[row_idx] = estimated_mb;
    }
}

void RegisterTsEstimateBacktestMemoryFunction(ExtensionLoader &loader) {
    ScalarFunctionSet func_set("ts_estimate_backtest_memory");
    func_set.AddFunction(ScalarFunction(
        {LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT},
        LogicalType::BIGINT,
        TsEstimateBacktestMemoryFunction
    ));
    {
        CreateScalarFunctionInfo info(func_set);
        FunctionDescription desc;
        desc.description = "Estimates memory usage in MB for a ts_cv_folds_by + ts_cv_forecast_by two-step cross-validation run given series count, length, folds, and horizon.";
        desc.examples = {"ts_estimate_backtest_memory(100, 365, 3, 30)"};
        desc.categories = {"time-series", "utilities"};
        desc.parameter_names = {"n_series", "n_dates", "folds", "horizon"};
        desc.parameter_types = {LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

} // namespace duckdb
