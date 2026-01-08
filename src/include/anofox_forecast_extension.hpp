#pragma once

#include "duckdb.hpp"

namespace duckdb {

// Forward declarations for function registration
void RegisterTsStatsFunction(ExtensionLoader &loader);
void RegisterTsQualityReportFunction(ExtensionLoader &loader);
void RegisterTsStatsSummaryFunction(ExtensionLoader &loader);
void RegisterTsDataQualityFunction(ExtensionLoader &loader);
void RegisterTsDataQualitySummaryFunction(ExtensionLoader &loader);
void RegisterTsFillGapsFunction(ExtensionLoader &loader);
void RegisterTsFillGapsOperatorFunction(ExtensionLoader &loader);
void RegisterTsFillForwardFunction(ExtensionLoader &loader);
void RegisterTsFillForwardOperatorFunction(ExtensionLoader &loader);
void RegisterTsDropConstantFunction(ExtensionLoader &loader);
void RegisterTsDropShortFunction(ExtensionLoader &loader);
void RegisterTsDropLeadingZerosFunction(ExtensionLoader &loader);
void RegisterTsDropTrailingZerosFunction(ExtensionLoader &loader);
void RegisterTsDropEdgeZerosFunction(ExtensionLoader &loader);
void RegisterTsFillNullsConstFunction(ExtensionLoader &loader);
void RegisterTsFillNullsForwardFunction(ExtensionLoader &loader);
void RegisterTsFillNullsBackwardFunction(ExtensionLoader &loader);
void RegisterTsFillNullsMeanFunction(ExtensionLoader &loader);
void RegisterTsDiffFunction(ExtensionLoader &loader);
void RegisterTsDetectSeasonalityFunction(ExtensionLoader &loader);
void RegisterTsAnalyzeSeasonalityFunction(ExtensionLoader &loader);
void RegisterTsMstlDecompositionFunction(ExtensionLoader &loader);

// Period detection functions (fdars-core integration)
void RegisterTsDetectPeriodsFunction(ExtensionLoader &loader);
void RegisterTsEstimatePeriodFftFunction(ExtensionLoader &loader);
void RegisterTsEstimatePeriodAcfFunction(ExtensionLoader &loader);
void RegisterTsDetectMultiplePeriodsFunction(ExtensionLoader &loader);
void RegisterTsAutoperiodFunction(ExtensionLoader &loader);
void RegisterTsCfdAutoperiodFunction(ExtensionLoader &loader);
void RegisterTsLombScargleFunction(ExtensionLoader &loader);
void RegisterTsAicPeriodFunction(ExtensionLoader &loader);
void RegisterTsSsaPeriodFunction(ExtensionLoader &loader);
void RegisterTsStlPeriodFunction(ExtensionLoader &loader);
void RegisterTsMatrixProfilePeriodFunction(ExtensionLoader &loader);
void RegisterTsSazedPeriodFunction(ExtensionLoader &loader);

// Peak detection functions (fdars-core integration)
void RegisterTsDetectPeaksFunction(ExtensionLoader &loader);
void RegisterTsAnalyzePeakTimingFunction(ExtensionLoader &loader);

// Detrending and decomposition functions (fdars-core integration)
void RegisterTsDetrendFunction(ExtensionLoader &loader);
void RegisterTsDecomposeSeasonalFunction(ExtensionLoader &loader);

// Extended seasonality functions (fdars-core integration)
void RegisterTsSeasonalStrengthFunction(ExtensionLoader &loader);
void RegisterTsSeasonalStrengthWindowedFunction(ExtensionLoader &loader);
void RegisterTsClassifySeasonalityFunction(ExtensionLoader &loader);
void RegisterTsDetectSeasonalityChangesFunction(ExtensionLoader &loader);
void RegisterTsInstantaneousPeriodFunction(ExtensionLoader &loader);
void RegisterTsDetectAmplitudeModulationFunction(ExtensionLoader &loader);
void RegisterTsDetectChangepointsFunction(ExtensionLoader &loader);
void RegisterTsDetectChangepointsBocpdFunction(ExtensionLoader &loader);
void RegisterTsDetectChangepointsByFunction(ExtensionLoader &loader);
void RegisterTsDetectChangepointsAggFunction(ExtensionLoader &loader);
void RegisterTsFeaturesFunction(ExtensionLoader &loader);
void RegisterTsFeaturesListFunction(ExtensionLoader &loader);
void RegisterTsFeaturesAggFunction(ExtensionLoader &loader);
void RegisterTsFeaturesConfigFromJsonFunction(ExtensionLoader &loader);
void RegisterTsFeaturesConfigFromCsvFunction(ExtensionLoader &loader);
void RegisterTsFeaturesConfigTemplateFunction(ExtensionLoader &loader);
void RegisterTsForecastFunction(ExtensionLoader &loader);
void RegisterTsForecastByFunction(ExtensionLoader &loader);
void RegisterTsForecastAggFunction(ExtensionLoader &loader);
void RegisterTsMaeFunction(ExtensionLoader &loader);
void RegisterTsMseFunction(ExtensionLoader &loader);
void RegisterTsRmseFunction(ExtensionLoader &loader);
void RegisterTsMapeFunction(ExtensionLoader &loader);
void RegisterTsSmapeFunction(ExtensionLoader &loader);
void RegisterTsMaseFunction(ExtensionLoader &loader);
void RegisterTsR2Function(ExtensionLoader &loader);
void RegisterTsBiasFunction(ExtensionLoader &loader);
void RegisterTsRmaeFunction(ExtensionLoader &loader);
void RegisterTsQuantileLossFunction(ExtensionLoader &loader);
void RegisterTsMqlossFunction(ExtensionLoader &loader);
void RegisterTsCoverageFunction(ExtensionLoader &loader);

// Table macros
void RegisterTsTableMacros(ExtensionLoader &loader);

// Extension class
class AnofoxForecastExtension : public Extension {
public:
    void Load(ExtensionLoader &loader) override;
    std::string Name() override;
    std::string Version() const override;
};

} // namespace duckdb
