#pragma once

#include "duckdb.hpp"

namespace duckdb {

class ExtensionLoader;

// Register Data Preparation table macros
void RegisterDataPrepMacros(ExtensionLoader &loader);

} // namespace duckdb

