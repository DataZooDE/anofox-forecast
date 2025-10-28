#pragma once

#include "duckdb.hpp"

namespace duckdb {

class ExtensionLoader;

// Register seasonality detection and analysis functions
void RegisterSeasonalityFunction(ExtensionLoader &loader);

} // namespace duckdb
