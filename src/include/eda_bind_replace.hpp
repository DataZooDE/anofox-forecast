#pragma once

#include "duckdb.hpp"

namespace duckdb {

// TS_STATS bind_replace function (unified, handles both VARCHAR and INTEGER frequency)
unique_ptr<TableRef> TSStatsBindReplace(ClientContext &context, TableFunctionBindInput &input);

// TS_QUALITY_REPORT bind_replace function
unique_ptr<TableRef> TSQualityReportBindReplace(ClientContext &context, TableFunctionBindInput &input);

} // namespace duckdb
