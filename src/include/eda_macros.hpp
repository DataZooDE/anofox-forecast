#pragma once

#include "duckdb.hpp"

namespace duckdb {

class ExtensionLoader;

// Register EDA (Exploratory Data Analysis) table functions (bind_replace)
void RegisterEDATableFunctions(ExtensionLoader &loader);

// Register EDA (Exploratory Data Analysis) table macros
void RegisterEDAMacros(ExtensionLoader &loader);

// Register Data Quality table macros
void RegisterDataQualityMacros(ExtensionLoader &loader);

} // namespace duckdb
