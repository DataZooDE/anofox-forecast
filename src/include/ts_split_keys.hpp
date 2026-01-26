#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

//! Register the ts_split_keys streaming table function
//! Splits a combined unique_id back into its original component columns
//! Auto-detects the number of parts from the data
//! Parameters via MAP{}:
//!   - separator (default '|')
//!   - columns (optional LIST of column names)
void RegisterTsSplitKeysFunction(ExtensionLoader &loader);

} // namespace duckdb
