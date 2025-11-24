#pragma once

#include "duckdb.hpp"

namespace duckdb {

class ExtensionLoader;

// Register Data Quality table macros
void RegisterDataQualityMacros(ExtensionLoader &loader);

} // namespace duckdb
