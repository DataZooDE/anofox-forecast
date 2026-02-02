#pragma once

#include "duckdb.hpp"

namespace duckdb {

// Registration functions for native metrics table functions
void RegisterTsMetricsNativeFunction(ExtensionLoader &loader);
void RegisterTsMaseNativeFunction(ExtensionLoader &loader);
void RegisterTsRmaeNativeFunction(ExtensionLoader &loader);
void RegisterTsCoverageNativeFunction(ExtensionLoader &loader);
void RegisterTsQuantileLossNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
