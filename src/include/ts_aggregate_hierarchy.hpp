#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

//! Register the ts_aggregate_hierarchy streaming table function
//! Supports arbitrary hierarchy levels (2-N ID columns)
//! Parameters via MAP{}: separator, aggregate_keyword
void RegisterTsAggregateHierarchyFunction(ExtensionLoader &loader);

} // namespace duckdb
