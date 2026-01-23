#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"

namespace duckdb {

// Registration function
void RegisterTsBacktestNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
