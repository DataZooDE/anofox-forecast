#pragma once

#include "duckdb.hpp"

namespace duckdb {

// TS_STATS bind_replace functions
unique_ptr<TableRef> TSStatsVarcharBindReplace(ClientContext &context, TableFunctionBindInput &input);
unique_ptr<TableRef> TSStatsIntegerBindReplace(ClientContext &context, TableFunctionBindInput &input);

// TS_QUALITY_REPORT bind_replace function
unique_ptr<TableRef> TSQualityReportBindReplace(ClientContext &context, TableFunctionBindInput &input);

} // namespace duckdb
