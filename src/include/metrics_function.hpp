#pragma once

#include "duckdb.hpp"

namespace duckdb {

class ExtensionLoader;

// Register the TS_METRICS scalar function
void RegisterMetricsFunction(ExtensionLoader &loader);

} // namespace duckdb
