#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

//! Register the _ts_detect_changepoints_native streaming table function
void RegisterTsDetectChangepointsNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
