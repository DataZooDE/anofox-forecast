#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

//! Register the _ts_stats_native streaming table function
void RegisterTsStatsNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
