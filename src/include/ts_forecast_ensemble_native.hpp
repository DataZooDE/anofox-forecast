#pragma once

#include "duckdb.hpp"

namespace duckdb {

// Phase 5: ENS-02 — Explicit-member ensemble scalar function
// Registers _ts_forecast_ensemble_native as a ScalarFunction following
// the _ts_forecast_scalar precedent (per-series, GROUP BY, unnest shape).
void RegisterTsForecastEnsembleNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
