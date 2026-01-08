#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

#include "duckdb/function/scalar_function.hpp"
#include "duckdb/common/vector_operations/generic_executor.hpp"

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
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsMaeFunction
    ));
    loader.RegisterFunction(ts_mae_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mae");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsMaeFunction
    ));
    loader.RegisterFunction(anofox_set);
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
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsMseFunction
    ));
    loader.RegisterFunction(ts_mse_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mse");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsMseFunction
    ));
    loader.RegisterFunction(anofox_set);
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
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsRmseFunction
    ));
    loader.RegisterFunction(ts_rmse_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_rmse");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsRmseFunction
    ));
    loader.RegisterFunction(anofox_set);
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
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsMapeFunction
    ));
    loader.RegisterFunction(ts_mape_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mape");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsMapeFunction
    ));
    loader.RegisterFunction(anofox_set);
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
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsSmapeFunction
    ));
    loader.RegisterFunction(ts_smape_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_smape");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsSmapeFunction
    ));
    loader.RegisterFunction(anofox_set);
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
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsMaseFunction
    ));
    loader.RegisterFunction(ts_mase_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mase");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsMaseFunction
    ));
    loader.RegisterFunction(anofox_set);
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
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsR2Function
    ));
    loader.RegisterFunction(ts_r2_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_r2");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsR2Function
    ));
    loader.RegisterFunction(anofox_set);
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
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsBiasFunction
    ));
    loader.RegisterFunction(ts_bias_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_bias");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsBiasFunction
    ));
    loader.RegisterFunction(anofox_set);
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
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsRmaeFunction
    ));
    loader.RegisterFunction(ts_rmae_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_rmae");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsRmaeFunction
    ));
    loader.RegisterFunction(anofox_set);
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
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        LogicalType::DOUBLE,
        TsQuantileLossFunction
    ));
    loader.RegisterFunction(ts_ql_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_quantile_loss");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::DOUBLE},
        LogicalType::DOUBLE,
        TsQuantileLossFunction
    ));
    loader.RegisterFunction(anofox_set);
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
        {LogicalType::LIST(LogicalType::DOUBLE),
         LogicalType::LIST(LogicalType::LIST(LogicalType::DOUBLE)),
         LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsMqlossFunction
    ));
    loader.RegisterFunction(ts_mqloss_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_mqloss");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE),
         LogicalType::LIST(LogicalType::LIST(LogicalType::DOUBLE)),
         LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsMqlossFunction
    ));
    loader.RegisterFunction(anofox_set);
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
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsCoverageFunction
    ));
    loader.RegisterFunction(ts_coverage_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_coverage");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE), LogicalType::LIST(LogicalType::DOUBLE)},
        LogicalType::DOUBLE,
        TsCoverageFunction
    ));
    loader.RegisterFunction(anofox_set);
}

} // namespace duckdb
