#define DUCKDB_EXTENSION_MAIN

#include "anofox_forecast_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"

namespace duckdb {

static void LoadInternal(ExtensionLoader &loader) {
    // Register EDA functions
    RegisterTsStatsFunction(loader);
    RegisterTsQualityReportFunction(loader);
    RegisterTsStatsSummaryFunction(loader);

    // Register Data Quality functions
    RegisterTsDataQualityFunction(loader);
    RegisterTsDataQualitySummaryFunction(loader);

    // Register Gap Filling functions
    RegisterTsFillGapsFunction(loader);
    RegisterTsFillGapsOperatorFunction(loader);
    RegisterTsFillForwardFunction(loader);

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

    // Register Table Macros
    RegisterTsTableMacros(loader);
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
