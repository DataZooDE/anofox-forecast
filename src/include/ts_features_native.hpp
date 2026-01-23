#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

//! Register the _ts_features_native streaming table function
void RegisterTsFeaturesNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
