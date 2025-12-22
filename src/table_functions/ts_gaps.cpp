#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/function/scalar_function.hpp"

namespace duckdb {

// ============================================================================
// ts_fill_gaps_operator - Table function for gap filling
//
// C++ API: ts_fill_gaps_operator(source_table, group_col, date_col, value_col, frequency)
//
// Note: This is functionally identical to the ts_fill_gaps table macro.
// The "operator" version is provided for API compatibility with the C++ extension.
// Both use DuckDB's query optimizer for efficient execution.
//
// For truly native high-performance streaming, a different approach using
// DuckDB's operator framework would be needed.
// ============================================================================

void RegisterTsFillGapsOperatorFunction(ExtensionLoader &loader) {
    // ts_fill_gaps_operator is implemented as a table macro in ts_macros.cpp
    // This function is intentionally empty - the macro provides the implementation
    // with identical functionality and parameters.
}

// Placeholder functions for other gap-related operations
void RegisterTsFillGapsFunction(ExtensionLoader &loader) {
    // Implemented as table macro in ts_macros.cpp
}

void RegisterTsFillForwardFunction(ExtensionLoader &loader) {
    // Implemented as table macro in ts_macros.cpp
}

// ============================================================================
// Placeholder functions for summary operations (implemented as macros)
// ============================================================================

void RegisterTsQualityReportFunction(ExtensionLoader &loader) {
    // Implemented as table macro in ts_macros.cpp
}

void RegisterTsStatsSummaryFunction(ExtensionLoader &loader) {
    // Implemented as table macro in ts_macros.cpp
}

} // namespace duckdb
