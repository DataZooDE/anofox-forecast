#pragma once

#include "duckdb.hpp"

namespace duckdb {

class ExtensionLoader;

// Register changepoint detection functions
void RegisterChangepointFunction(ExtensionLoader &loader);

} // namespace duckdb
