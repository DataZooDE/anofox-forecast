#pragma once

#include "duckdb.hpp"

namespace duckdb {

// Fill NULLs macros (4-5 params)
unique_ptr<TableRef> TSFillNullsForwardBindReplace(ClientContext &context, TableFunctionBindInput &input);
unique_ptr<TableRef> TSFillNullsBackwardBindReplace(ClientContext &context, TableFunctionBindInput &input);
unique_ptr<TableRef> TSFillNullsMeanBindReplace(ClientContext &context, TableFunctionBindInput &input);
unique_ptr<TableRef> TSFillNullsConstBindReplace(ClientContext &context, TableFunctionBindInput &input);

// Fill gaps macros (5 params)
unique_ptr<TableRef> TSFillGapsBindReplace(ClientContext &context, TableFunctionBindInput &input); // VARCHAR frequency
unique_ptr<TableRef> TSFillGapsIntegerBindReplace(ClientContext &context,
                                                  TableFunctionBindInput &input); // INTEGER frequency

// Fill forward macros (6 params)
unique_ptr<TableRef> TSFillForwardVarcharBindReplace(ClientContext &context,
                                                     TableFunctionBindInput &input); // VARCHAR frequency
unique_ptr<TableRef> TSFillForwardIntegerBindReplace(ClientContext &context,
                                                     TableFunctionBindInput &input); // INTEGER frequency

// Drop macros (3-5 params)
unique_ptr<TableRef> TSDropConstantBindReplace(ClientContext &context, TableFunctionBindInput &input);
unique_ptr<TableRef> TSDropShortBindReplace(ClientContext &context, TableFunctionBindInput &input);
unique_ptr<TableRef> TSDropZerosBindReplace(ClientContext &context, TableFunctionBindInput &input);
unique_ptr<TableRef> TSDropLeadingZerosBindReplace(ClientContext &context, TableFunctionBindInput &input);
unique_ptr<TableRef> TSDropTrailingZerosBindReplace(ClientContext &context, TableFunctionBindInput &input);
unique_ptr<TableRef> TSDropGappyBindReplace(ClientContext &context, TableFunctionBindInput &input);
unique_ptr<TableRef> TSDropEdgeZerosBindReplace(ClientContext &context, TableFunctionBindInput &input);

// Other macros
unique_ptr<TableRef> TSDiffBindReplace(ClientContext &context, TableFunctionBindInput &input);

} // namespace duckdb
