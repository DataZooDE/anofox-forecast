#pragma once

#include "duckdb.hpp"

namespace duckdb {

// Phase 6: INSP-01 — Ensemble member introspection scalar functions
// Registers _ts_ensemble_inspect_native and _ts_auto_ensemble_inspect_native
// as ScalarFunctions following the Phase 5 _ts_forecast_ensemble_native precedent.
void RegisterTsEnsembleInspectNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
