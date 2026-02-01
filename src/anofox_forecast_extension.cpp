#define DUCKDB_EXTENSION_MAIN

#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/main/extension_helper.hpp"

#include <cstdlib>

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
    // Auto-load json extension (required for STRUCT params syntax in table macros)
    auto &db = loader.GetDatabaseInstance();
    ExtensionHelper::TryAutoLoadExtension(db, "json");

    // Register EDA functions
    RegisterTsStatsFunction(loader);
    RegisterTsStatsByFunction(loader);  // Native table function for ts_stats_by
    RegisterTsQualityReportFunction(loader);
    RegisterTsStatsSummaryFunction(loader);

    // Register Data Quality functions
    RegisterTsDataQualityFunction(loader);
    RegisterTsDataQualitySummaryFunction(loader);

    // Register Gap Filling functions
    RegisterTsFillGapsFunction(loader);
    RegisterTsFillGapsOperatorFunction(loader);
    RegisterTsFillGapsNativeFunction(loader);
    RegisterTsFillForwardFunction(loader);
    RegisterTsFillForwardOperatorFunction(loader);
    RegisterTsFillForwardNativeFunction(loader);

    // Register Filtering functions
    RegisterTsDropConstantFunction(loader);
    RegisterTsDropShortFunction(loader);

    // Register Edge Cleaning functions
    RegisterTsDropLeadingZerosFunction(loader);
    RegisterTsDropTrailingZerosFunction(loader);
    RegisterTsDropEdgeZerosFunction(loader);

    // Register Imputation functions
    RegisterTsFillNullsConstFunction(loader);
    RegisterTsFillNullsForwardFunction(loader);
    RegisterTsFillNullsBackwardFunction(loader);
    RegisterTsFillNullsMeanFunction(loader);

    // Register Transform functions
    RegisterTsDiffFunction(loader);

    // Register Seasonality functions
    RegisterTsDetectSeasonalityFunction(loader);
    RegisterTsAnalyzeSeasonalityFunction(loader);

    // Register Period Detection functions (fdars-core)
    RegisterTsDetectPeriodsFunction(loader);
    RegisterTsEstimatePeriodFftFunction(loader);
    RegisterTsEstimatePeriodAcfFunction(loader);
    RegisterTsDetectMultiplePeriodsFunction(loader);
    RegisterTsDetectPeriodsAggFunction(loader);
    RegisterTsAutoperiodFunction(loader);
    RegisterTsCfdAutoperiodFunction(loader);
    RegisterTsLombScargleFunction(loader);
    RegisterTsAicPeriodFunction(loader);
    RegisterTsSsaPeriodFunction(loader);
    RegisterTsStlPeriodFunction(loader);
    RegisterTsMatrixProfilePeriodFunction(loader);
    RegisterTsSazedPeriodFunction(loader);

    // Register Peak Detection functions (fdars-core)
    RegisterTsDetectPeaksFunction(loader);
    RegisterTsAnalyzePeakTimingFunction(loader);

    // Register Detrending functions (fdars-core)
    RegisterTsDetrendFunction(loader);
    RegisterTsDecomposeSeasonalFunction(loader);

    // Register Extended Seasonality functions (fdars-core)
    RegisterTsSeasonalStrengthFunction(loader);
    RegisterTsSeasonalStrengthWindowedFunction(loader);
    RegisterTsClassifySeasonalityFunction(loader);
    RegisterTsClassifySeasonalityAggFunction(loader);
    RegisterTsDetectSeasonalityChangesFunction(loader);
    RegisterTsInstantaneousPeriodFunction(loader);
    RegisterTsDetectAmplitudeModulationFunction(loader);

    // Register Decomposition functions
    RegisterTsMstlDecompositionFunction(loader);

    // Register Changepoint Detection functions
    RegisterTsDetectChangepointsFunction(loader);
    RegisterTsDetectChangepointsBocpdFunction(loader);
    RegisterTsDetectChangepointsByFunction(loader);
    RegisterTsDetectChangepointsAggFunction(loader);

    // Register Feature Extraction functions
    RegisterTsFeaturesFunction(loader);
    RegisterTsFeaturesListFunction(loader);
    RegisterTsFeaturesAggFunction(loader);
    RegisterTsStatsAggFunction(loader);
    RegisterTsDataQualityAggFunction(loader);
    RegisterTsFeaturesConfigFromJsonFunction(loader);
    RegisterTsFeaturesConfigFromCsvFunction(loader);
    RegisterTsFeaturesConfigTemplateFunction(loader);

    // Register Forecasting functions
    RegisterTsForecastFunction(loader);
    RegisterTsForecastByFunction(loader);
    RegisterTsForecastAggFunction(loader);

    // Register Metric functions
    RegisterTsMaeFunction(loader);
    RegisterTsMseFunction(loader);
    RegisterTsRmseFunction(loader);
    RegisterTsMapeFunction(loader);
    RegisterTsSmapeFunction(loader);
    RegisterTsMaseFunction(loader);
    RegisterTsR2Function(loader);
    RegisterTsBiasFunction(loader);
    RegisterTsRmaeFunction(loader);
    RegisterTsQuantileLossFunction(loader);
    RegisterTsMqlossFunction(loader);
    RegisterTsCoverageFunction(loader);
    RegisterTsEstimateBacktestMemoryFunction(loader);

    // Register Conformal Prediction functions
    RegisterTsConformalQuantileFunction(loader);
    RegisterTsConformalIntervalsFunction(loader);
    RegisterTsConformalPredictFunction(loader);
    RegisterTsConformalPredictAsymmetricFunction(loader);
    RegisterTsMeanIntervalWidthFunction(loader);

    // Register Conformal API v2 (Learn/Apply pattern)
    RegisterTsConformalLearnFunction(loader);
    RegisterTsConformalApplyFunction(loader);
    RegisterTsConformalCoverageFunction(loader);
    RegisterTsConformalEvaluateFunction(loader);

    // Register Table Macros
    RegisterTsTableMacros(loader);

    // Register Native Table Functions (streaming)
    RegisterTsBacktestNativeFunction(loader);
    RegisterTsForecastNativeFunction(loader);
    RegisterTsCvSplitNativeFunction(loader);
    RegisterTsCvForecastNativeFunction(loader);
    RegisterTsCvGenerateFoldsNativeFunction(loader);
    RegisterTsCvFoldsNativeFunction(loader);
    RegisterTsMstlDecompositionNativeFunction(loader);
    RegisterTsFeaturesNativeFunction(loader);
    RegisterTsAggregateHierarchyFunction(loader);
    RegisterTsCombineKeysFunction(loader);
    RegisterTsSplitKeysFunction(loader);
    RegisterTsValidateSeparatorFunction(loader);

    // Initialize telemetry (respects DATAZOO_DISABLE_TELEMETRY env var)
    anofox_telemetry_init(true, nullptr);
    anofox_telemetry_capture_extension_load();
}

void AnofoxForecastExtension::Load(ExtensionLoader &loader) {
    LoadInternal(loader);
}

std::string AnofoxForecastExtension::Name() {
    return "anofox_forecast";
}

std::string AnofoxForecastExtension::Version() const {
#ifdef EXT_VERSION_ANOFOX_FORECAST
    return EXT_VERSION_ANOFOX_FORECAST;
#else
    return "0.1.0";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(anofox_forecast, loader) {
    duckdb::LoadInternal(loader);
}

}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
