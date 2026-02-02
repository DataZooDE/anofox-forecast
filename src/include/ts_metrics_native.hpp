#pragma once

#include "duckdb.hpp"

namespace duckdb {

// Registration function for native metrics table function
void RegisterTsMetricsNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
