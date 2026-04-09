#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

namespace duckdb {

// Helper to extract values from LIST vectors
static void ExtractListAsDouble(Vector &list_vec, idx_t row_idx, vector<double> &out_values) {
    auto list_data = ListVector::GetData(list_vec);
    auto &list_entry = list_data[row_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);
    auto child_data = FlatVector::GetData<double>(child_vec);
    auto &child_validity = FlatVector::Validity(child_vec);

    out_values.clear();
    out_values.reserve(list_entry.length);

    for (idx_t i = 0; i < list_entry.length; i++) {
        idx_t child_idx = list_entry.offset + i;
        if (child_validity.RowIsValid(child_idx)) {
            out_values.push_back(child_data[child_idx]);
        }
    }
}

// ============================================================================
// ts_mae - Mean Absolute Error
// ============================================================================

static void TsMaeFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &forecast_vec = args.data[1];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(actual_vec, row_idx) || FlatVector::IsNull(forecast_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actual, forecast;
        ExtractListAsDouble(actual_vec, row_idx, actual);
        ExtractListAsDouble(forecast_vec, row_idx, forecast);

        AnofoxError error;
        double mae_result;
        bool success = anofox_ts_mae(
            actual.data(), actual.size(),
            forecast.data(), forecast.size(),
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

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(actual_vec, row_idx) || FlatVector::IsNull(forecast_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actual, forecast;
        ExtractListAsDouble(actual_vec, row_idx, actual);
        ExtractListAsDouble(forecast_vec, row_idx, forecast);

        AnofoxError error;
        double mse_result;
        bool success = anofox_ts_mse(
            actual.data(), actual.size(),
            forecast.data(), forecast.size(),
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

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(actual_vec, row_idx) || FlatVector::IsNull(forecast_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actual, forecast;
        ExtractListAsDouble(actual_vec, row_idx, actual);
        ExtractListAsDouble(forecast_vec, row_idx, forecast);

        AnofoxError error;
        double rmse_result;
        bool success = anofox_ts_rmse(
            actual.data(), actual.size(),
            forecast.data(), forecast.size(),
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

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(actual_vec, row_idx) || FlatVector::IsNull(forecast_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actual, forecast;
        ExtractListAsDouble(actual_vec, row_idx, actual);
        ExtractListAsDouble(forecast_vec, row_idx, forecast);

        AnofoxError error;
        double mape_result;
        bool success = anofox_ts_mape(
            actual.data(), actual.size(),
            forecast.data(), forecast.size(),
            &mape_result, &error
        );

        if (!success) {
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
        desc.description = "Computes Mean Absolute Percentage Error (MAPE) between actual and predicted value arrays.";
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
        desc.description = "Computes Mean Absolute Percentage Error (MAPE) between actual and predicted value arrays.";
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

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(actual_vec, row_idx) || FlatVector::IsNull(forecast_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actual, forecast;
        ExtractListAsDouble(actual_vec, row_idx, actual);
        ExtractListAsDouble(forecast_vec, row_idx, forecast);

        AnofoxError error;
        double smape_result;
        bool success = anofox_ts_smape(
            actual.data(), actual.size(),
            forecast.data(), forecast.size(),
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
// ts_mase - Mean Absolute Scaled Error
// C++ API: ts_mase(actual[], predicted[], baseline[]) → DOUBLE
// ============================================================================

static void TsMaseFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &forecast_vec = args.data[1];
    auto &baseline_vec = args.data[2];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(actual_vec, row_idx) || FlatVector::IsNull(forecast_vec, row_idx) ||
            FlatVector::IsNull(baseline_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actual, forecast, baseline;
        ExtractListAsDouble(actual_vec, row_idx, actual);
        ExtractListAsDouble(forecast_vec, row_idx, forecast);
        ExtractListAsDouble(baseline_vec, row_idx, baseline);

        AnofoxError error;
        double mase_result;
        bool success = anofox_ts_mase(
            actual.data(), actual.size(),
            forecast.data(), forecast.size(),
            baseline.data(), baseline.size(),
            &mase_result, &error
        );

        if (!success) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        result_data[row_idx] = mase_result;
    }
}

void RegisterTsMaseFunction(ExtensionLoader &loader) {
    ScalarFunctionSet ts_mase_set("ts_mase");
    ts_mase_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMaseFunction
    ));
    {
        CreateScalarFunctionInfo info(ts_mase_set);
        FunctionDescription desc;
        desc.description = "Computes Mean Absolute Scaled Error (MASE) comparing forecast error to a baseline model.";
        desc.examples = {"ts_mase(LIST(actual ORDER BY date), LIST(forecast ORDER BY date), LIST(naive ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted", "baseline"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mase");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))},
        LogicalType(LogicalTypeId::DOUBLE),
        TsMaseFunction
    ));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_mase";
        FunctionDescription desc;
        desc.description = "Computes Mean Absolute Scaled Error (MASE) comparing forecast error to a baseline model.";
        desc.examples = {"ts_mase(LIST(actual ORDER BY date), LIST(forecast ORDER BY date), LIST(naive ORDER BY date))"};
        desc.categories = {"time-series", "metrics"};
        desc.parameter_names = {"actual", "predicted", "baseline"};
        desc.parameter_types = {LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE)), LogicalType::LIST(LogicalType(LogicalTypeId::DOUBLE))};
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

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(actual_vec, row_idx) || FlatVector::IsNull(forecast_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actual, forecast;
        ExtractListAsDouble(actual_vec, row_idx, actual);
        ExtractListAsDouble(forecast_vec, row_idx, forecast);

        AnofoxError error;
        double r2_result;
        bool success = anofox_ts_r2(
            actual.data(), actual.size(),
            forecast.data(), forecast.size(),
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

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(actual_vec, row_idx) || FlatVector::IsNull(forecast_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actual, forecast;
        ExtractListAsDouble(actual_vec, row_idx, actual);
        ExtractListAsDouble(forecast_vec, row_idx, forecast);

        AnofoxError error;
        double bias_result;
        bool success = anofox_ts_bias(
            actual.data(), actual.size(),
            forecast.data(), forecast.size(),
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

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(actual_vec, row_idx) || FlatVector::IsNull(pred1_vec, row_idx) ||
            FlatVector::IsNull(pred2_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actual, pred1, pred2;
        ExtractListAsDouble(actual_vec, row_idx, actual);
        ExtractListAsDouble(pred1_vec, row_idx, pred1);
        ExtractListAsDouble(pred2_vec, row_idx, pred2);

        AnofoxError error;
        double rmae_result;
        bool success = anofox_ts_rmae(
            actual.data(), actual.size(),
            pred1.data(), pred1.size(),
            pred2.data(), pred2.size(),
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

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        // Get quantile from unified format first (handles constant vectors correctly)
        auto quantile_idx = quantile_data.sel->get_index(row_idx);
        if (FlatVector::IsNull(actual_vec, row_idx) || FlatVector::IsNull(forecast_vec, row_idx) ||
            !quantile_data.validity.RowIsValid(quantile_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actual, forecast;
        ExtractListAsDouble(actual_vec, row_idx, actual);
        ExtractListAsDouble(forecast_vec, row_idx, forecast);

        double quantile = UnifiedVectorFormat::GetData<double>(quantile_data)[quantile_idx];

        AnofoxError error;
        double ql_result;
        bool success = anofox_ts_quantile_loss(
            actual.data(), actual.size(),
            forecast.data(), forecast.size(),
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

// Helper to extract nested LIST (2D array) as vector of vectors
static void ExtractNestedListAsDouble(Vector &list_vec, idx_t row_idx, vector<vector<double>> &out_values) {
    auto list_data = ListVector::GetData(list_vec);
    auto &list_entry = list_data[row_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);  // This is also a LIST
    auto child_list_data = ListVector::GetData(child_vec);

    auto &grandchild_vec = ListVector::GetEntry(child_vec);  // The actual DOUBLE values
    auto grandchild_data = FlatVector::GetData<double>(grandchild_vec);
    auto &grandchild_validity = FlatVector::Validity(grandchild_vec);

    out_values.clear();
    out_values.reserve(list_entry.length);

    for (idx_t i = 0; i < list_entry.length; i++) {
        idx_t child_idx = list_entry.offset + i;
        auto &inner_list = child_list_data[child_idx];

        vector<double> inner_values;
        inner_values.reserve(inner_list.length);

        for (idx_t j = 0; j < inner_list.length; j++) {
            idx_t gc_idx = inner_list.offset + j;
            if (grandchild_validity.RowIsValid(gc_idx)) {
                inner_values.push_back(grandchild_data[gc_idx]);
            }
        }
        out_values.push_back(std::move(inner_values));
    }
}

static void TsMqlossFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &actual_vec = args.data[0];
    auto &quantiles_vec = args.data[1];  // LIST(LIST(DOUBLE)) - 2D array
    auto &levels_vec = args.data[2];      // LIST(DOUBLE) - quantile levels
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto result_data = FlatVector::GetData<double>(result);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(actual_vec, row_idx) || FlatVector::IsNull(quantiles_vec, row_idx) ||
            FlatVector::IsNull(levels_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actual, levels;
        vector<vector<double>> quantiles;
        ExtractListAsDouble(actual_vec, row_idx, actual);
        ExtractNestedListAsDouble(quantiles_vec, row_idx, quantiles);
        ExtractListAsDouble(levels_vec, row_idx, levels);

        if (quantiles.size() != levels.size()) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Build array of pointers for FFI
        vector<const double *> quantile_ptrs;
        for (const auto &q : quantiles) {
            quantile_ptrs.push_back(q.data());
        }

        AnofoxError error;
        double mqloss_result;
        bool success = anofox_ts_mqloss(
            actual.data(), actual.size(),
            quantile_ptrs.data(), quantiles.size(),
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

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(actual_vec, row_idx) || FlatVector::IsNull(lower_vec, row_idx) ||
            FlatVector::IsNull(upper_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> actual, lower, upper;
        ExtractListAsDouble(actual_vec, row_idx, actual);
        ExtractListAsDouble(lower_vec, row_idx, lower);
        ExtractListAsDouble(upper_vec, row_idx, upper);

        AnofoxError error;
        double cov_result;
        bool success = anofox_ts_coverage(
            actual.data(), actual.size(),
            lower.data(),
            upper.data(),
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

        // Estimate memory usage for ts_backtest_auto_by:
        // 1. cv_splits: n_series × n_dates × folds × 40 bytes (5 columns × 8 bytes)
        // 2. cv_train/cv_test: same as cv_splits but filtered (~60% train, ~10% test)
        // 3. forecast LIST aggregation: n_series × folds × (n_dates/2) × 8 bytes
        // 4. output: n_series × folds × horizon × 88 bytes (11 columns × 8 bytes)
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
        desc.description = "Estimates memory usage in MB for a ts_backtest_auto_by run given series count, length, folds, and horizon.";
        desc.examples = {"ts_estimate_backtest_memory(100, 365, 3, 30)"};
        desc.categories = {"time-series", "utilities"};
        desc.parameter_names = {"n_series", "n_dates", "folds", "horizon"};
        desc.parameter_types = {LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT, LogicalType::BIGINT};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

} // namespace duckdb
