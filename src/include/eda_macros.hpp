#pragma once

#include "duckdb.hpp"

namespace duckdb {

class ExtensionLoader;

// Register EDA (Exploratory Data Analysis) table macros
void RegisterEDAMacros(ExtensionLoader &loader);

} // namespace duckdb

