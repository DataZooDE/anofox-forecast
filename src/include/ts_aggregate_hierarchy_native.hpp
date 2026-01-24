#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

//! Register the ts_aggregate_hierarchy_native streaming table function
//! Supports arbitrary hierarchy levels (2-N ID columns)
void RegisterTsAggregateHierarchyNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
