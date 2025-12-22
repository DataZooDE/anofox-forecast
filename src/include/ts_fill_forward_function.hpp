#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include "ts_fill_gaps_function.hpp" // Reuse FrequencyConfig, SeriesData, and helper functions
#include <memory>
#include <vector>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <set>

namespace duckdb {

// Bind data for the TS_FILL_FORWARD function (Table-In-Out version)
struct TSFillForwardBindData : public TableFunctionData {
	std::string group_col;
	std::string date_col;
	std::string value_col;
	FrequencyConfig frequency;
	Value target_date_value;              // User-specified target date (NEW vs ts_fill_gaps)
	idx_t group_col_idx = 0;
	idx_t date_col_idx = 0;
	idx_t value_col_idx = 0;
	LogicalType date_col_type;            // DATE, TIMESTAMP, INTEGER, or BIGINT
	vector<LogicalType> return_types;     // Store return types from bind
	vector<string> return_names;          // Store return names from bind

	TSFillForwardBindData() = default;
};

// Global state for the TS_FILL_FORWARD function (Table-In-Out version)
struct TSFillForwardGlobalState : public GlobalTableFunctionState {
	const TSFillForwardBindData *bind_data = nullptr;
	idx_t group_col_idx = 0;
	idx_t date_col_idx = 0;
	idx_t value_col_idx = 0;

	TSFillForwardGlobalState() = default;

	// Override MaxThreads to return 1 to avoid BatchedDataCollection merge errors
	// See: https://github.com/duckdb/duckdb/issues/19939
	idx_t MaxThreads() const override {
		return 1;
	}
};

// Local state for the TS_FILL_FORWARD function
// Reuses SeriesData from ts_fill_gaps_function.hpp
struct TSFillForwardLocalState : public LocalTableFunctionState {
	std::unordered_map<std::string, SeriesData> series_data; // Group value (as string) -> SeriesData
	bool input_done = false;
	idx_t output_offset = 0;
	std::vector<std::string> current_group_order;            // Maintain order of groups (as strings)
	std::unordered_map<std::string, Value> group_value_map;  // Map string key back to original Value for output

	// Output generation state
	std::string current_group_key;                                         // Current group being processed
	idx_t current_group_index = 0;                                         // Index in current_group_order
	idx_t current_date_index = 0;                                          // Index in current group's date range
	std::vector<std::chrono::system_clock::time_point> current_date_range; // Current group's date range (temporary)
	std::vector<int64_t> current_integer_range;                            // Current group's integer range (temporary)
	bool is_integer_date_mode = false;                                     // True if using integer dates

	TSFillForwardLocalState() = default;
};

// Function declarations for Table-In-Out pattern
// Internal function: ts_fill_forward_operator (takes TABLE input)
unique_ptr<FunctionData> TSFillForwardOperatorBind(ClientContext &context, TableFunctionBindInput &input,
                                                   vector<LogicalType> &return_types, vector<string> &names);

unique_ptr<GlobalTableFunctionState> TSFillForwardOperatorInitGlobal(ClientContext &context,
                                                                     TableFunctionInitInput &input);

unique_ptr<LocalTableFunctionState> TSFillForwardOperatorInitLocal(ExecutionContext &context,
                                                                   TableFunctionInitInput &input,
                                                                   GlobalTableFunctionState *global_state);

// Table-in-out function: accumulates input data
OperatorResultType TSFillForwardOperatorInOut(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                              DataChunk &output);

// Finalize function: generates forward-filled output
OperatorFinalizeResultType TSFillForwardOperatorFinal(ExecutionContext &context, TableFunctionInput &data_p,
                                                      DataChunk &output);

unique_ptr<NodeStatistics> TSFillForwardCardinality(ClientContext &context, const FunctionData *bind_data);

// Create table-in-out function for internal use (takes TABLE input)
// Defined inline to avoid linker issues with static libraries (especially on Alpine/musl)
inline unique_ptr<TableFunction> CreateTSFillForwardOperatorTableFunction() {
	// Table-in-out function arguments: group_col, date_col, value_col, target_date, frequency
	// The input table columns are provided automatically via the input DataChunk
	vector<LogicalType> arguments = {
	    LogicalType::VARCHAR, // group_col
	    LogicalType::VARCHAR, // date_col
	    LogicalType::VARCHAR, // value_col
	    LogicalType::ANY,     // target_date (DATE, TIMESTAMP, or INTEGER)
	    LogicalType::ANY      // frequency (VARCHAR or INTEGER)
	};

	// Create table function with nullptr for regular function (we use in_out_function)
	TableFunction table_function(arguments, nullptr, TSFillForwardOperatorBind, TSFillForwardOperatorInitGlobal,
	                             TSFillForwardOperatorInitLocal);

	// Set in-out handlers
	table_function.in_out_function = TSFillForwardOperatorInOut;
	table_function.in_out_function_final = TSFillForwardOperatorFinal;
	table_function.cardinality = TSFillForwardCardinality;
	table_function.name = "anofox_fcst_ts_fill_forward_operator";

	// Named parameters
	table_function.named_parameters["group_col"] = LogicalType::VARCHAR;
	table_function.named_parameters["date_col"] = LogicalType::VARCHAR;
	table_function.named_parameters["value_col"] = LogicalType::VARCHAR;
	table_function.named_parameters["target_date"] = LogicalType::ANY;
	table_function.named_parameters["frequency"] = LogicalType::ANY;

	return make_uniq<TableFunction>(std::move(table_function));
}

} // namespace duckdb
