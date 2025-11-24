#pragma once

#include "duckdb/main/extension_loader.hpp"

namespace duckdb {

// Register Data Quality table macros
void RegisterDataQualityMacros(ExtensionLoader &loader);

} // namespace duckdb
