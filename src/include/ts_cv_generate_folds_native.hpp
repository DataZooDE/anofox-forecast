#pragma once

#include "duckdb.hpp"
#include "ts_fill_gaps_native.hpp"  // For shared types and helpers

namespace duckdb {

void RegisterTsCvGenerateFoldsNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
