#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

//! Register the ts_combine_keys streaming table function
//! Combines multiple ID columns into a single unique_id without aggregation
//! Supports arbitrary hierarchy levels (2-N ID columns)
//! Parameters via MAP{}: separator (default '|')
void RegisterTsCombineKeysFunction(ExtensionLoader &loader);

} // namespace duckdb
