#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/types/value.hpp"
#include <vector>
#include <string>

namespace duckdb {

void RegisterMstlDecompositionFunctions(ExtensionLoader &loader);

// Helper to extract seasonal periods from params (exposed for testing)
std::vector<int32_t> TSMstlExtractPeriods(const Value &params_val);

} // namespace duckdb
