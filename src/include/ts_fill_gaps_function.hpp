#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include <memory>
#include <vector>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <set>

namespace duckdb {

// Forward declarations for frequency types
enum class FrequencyType {
	VARCHAR_INTERVAL, // e.g., "1d", "1h", "30m"
	INTEGER_STEP      // e.g., 1, 2, 3
};

// Frequency configuration
struct FrequencyConfig {
	FrequencyType type;
	std::chrono::system_clock::duration interval; // For VARCHAR intervals
	int64_t step;                                 // For INTEGER steps
	std::string original_value;                   // For error messages
};

// Bind data for the TS_FILL_GAPS function (Table-In-Out version)
struct TSFillGapsBindData : public TableFunctionData {
	std::string group_col;
	std::string date_col;
	std::string value_col;
	FrequencyConfig frequency;
	idx_t group_col_idx = 0;
	idx_t date_col_idx = 0;
	idx_t value_col_idx = 0;
	LogicalType date_col_type;        // DATE, TIMESTAMP, INTEGER, or BIGINT
	vector<LogicalType> return_types; // Store return types from bind
	vector<string> return_names;      // Store return names from bind

	TSFillGapsBindData() = default;
};

// Global state for the TS_FILL_GAPS function (Table-In-Out version)
struct TSFillGapsGlobalState : public GlobalTableFunctionState {
	const TSFillGapsBindData *bind_data = nullptr;
	idx_t group_col_idx = 0;
	idx_t date_col_idx = 0;
	idx_t value_col_idx = 0;

	TSFillGapsGlobalState() = default;

	// Override MaxThreads to return 1 to avoid BatchedDataCollection merge errors
	// See: https://github.com/duckdb/duckdb/issues/19939
	idx_t MaxThreads() const override {
		return 1;
	}
};

// Series data structure for accumulating input per group
struct SeriesData {
	// Original input data (sparse - only existing dates)
	std::vector<std::chrono::system_clock::time_point> timestamps; // Original timestamps from input
	std::vector<int64_t> integer_dates;                            // Original integer dates from input
	std::vector<double> values;                                    // Original values (indexed by timestamp position)
	std::set<std::chrono::system_clock::time_point> timestamp_set; // For O(log n) gap detection
	std::unordered_set<int64_t> integer_date_set;                  // For INTEGER dates (O(1) lookup)
	std::vector<std::vector<Value>> other_columns;                 // Other columns (indexed by timestamp position)
	std::vector<Value> group_values;                               // Group values (indexed by timestamp position)

	// Generated date ranges (dense - all dates in range)
	std::vector<std::chrono::system_clock::time_point> generated_timestamps; // Full date range for output
	std::vector<int64_t> generated_integer_dates;                            // Full integer range for output

	// Lookup map: generated date -> index in original data (for value lookup)
	std::unordered_map<int64_t, idx_t> integer_date_to_index; // For INTEGER dates
	// For time_point, we'll use set lookup directly

	bool is_integer_date = false; // True if date_col is INTEGER/BIGINT

	SeriesData() = default;
};

// Local state for the TS_FILL_GAPS function
struct TSFillGapsLocalState : public LocalTableFunctionState {
	std::unordered_map<std::string, SeriesData> series_data; // Group value (as string) -> SeriesData
	bool input_done = false;
	idx_t output_offset = 0;
	std::vector<std::string> current_group_order;           // Maintain order of groups (as strings)
	std::unordered_map<std::string, Value> group_value_map; // Map string key back to original Value for output

	// Table scanning state (not needed with direct storage scan)

	// Output generation state
	std::string current_group_key;                                         // Current group being processed
	idx_t current_group_index = 0;                                         // Index in current_group_order
	idx_t current_date_index = 0;                                          // Index in current group's date range
	std::vector<std::chrono::system_clock::time_point> current_date_range; // Current group's date range (temporary)
	std::vector<int64_t> current_integer_range;                            // Current group's integer range (temporary)
	bool is_integer_date_mode = false;                                     // True if using integer dates

	TSFillGapsLocalState() = default;
};

// Helper function declarations (for unit testing)
namespace ts_fill_gaps_internal {

// Validate frequency compatibility with date column type
void ValidateFrequencyCompatibility(const LogicalType &date_col_type, const FrequencyConfig &frequency);

// Parse frequency string or integer to FrequencyConfig
FrequencyConfig ParseFrequency(const Value &frequency_value, const LogicalType &date_col_type);

// Convert frequency string to chrono duration
std::chrono::system_clock::duration ParseIntervalString(const std::string &freq_str);

// Generate date range for VARCHAR frequency
std::vector<std::chrono::system_clock::time_point> GenerateDateRange(std::chrono::system_clock::time_point min_date,
                                                                     std::chrono::system_clock::time_point max_date,
                                                                     std::chrono::system_clock::duration interval);

// Generate integer range for INTEGER frequency
std::vector<int64_t> GenerateIntegerRange(int64_t min_val, int64_t max_val, int64_t step);

} // namespace ts_fill_gaps_internal

// Function declarations for Table-In-Out pattern
// Internal function: ts_fill_gaps_operator (takes TABLE input)
unique_ptr<FunctionData> TSFillGapsOperatorBind(ClientContext &context, TableFunctionBindInput &input,
                                                vector<LogicalType> &return_types, vector<string> &names);

unique_ptr<GlobalTableFunctionState> TSFillGapsOperatorInitGlobal(ClientContext &context,
                                                                  TableFunctionInitInput &input);

unique_ptr<LocalTableFunctionState> TSFillGapsOperatorInitLocal(ExecutionContext &context,
                                                                TableFunctionInitInput &input,
                                                                GlobalTableFunctionState *global_state);

// Table-in-out function: accumulates input data
OperatorResultType TSFillGapsOperatorInOut(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                           DataChunk &output);

// Finalize function: generates gap-filled output
OperatorFinalizeResultType TSFillGapsOperatorFinal(ExecutionContext &context, TableFunctionInput &data_p,
                                                   DataChunk &output);

unique_ptr<NodeStatistics> TSFillGapsCardinality(ClientContext &context, const FunctionData *bind_data);

// Create table-in-out function for internal use (takes TABLE input)
// Defined inline to avoid linker issues with static libraries (especially on Alpine/musl)
inline unique_ptr<TableFunction> CreateTSFillGapsOperatorTableFunction() {
	// Table-in-out function arguments: group_col, date_col, value_col, frequency
	// The input table columns are provided automatically via the input DataChunk
	vector<LogicalType> arguments = {
	    LogicalType::VARCHAR, // group_col
	    LogicalType::VARCHAR, // date_col
	    LogicalType::VARCHAR, // value_col
	    LogicalType::ANY      // frequency (VARCHAR or INTEGER)
	};

	// Create table function with nullptr for regular function (we use in_out_function)
	TableFunction table_function(arguments, nullptr, TSFillGapsOperatorBind, TSFillGapsOperatorInitGlobal,
	                             TSFillGapsOperatorInitLocal);

	// Set in-out handlers
	table_function.in_out_function = TSFillGapsOperatorInOut;
	table_function.in_out_function_final = TSFillGapsOperatorFinal;
	table_function.cardinality = TSFillGapsCardinality;
	table_function.name = "anofox_fcst_ts_fill_gaps_operator";

	// Named parameters
	table_function.named_parameters["group_col"] = LogicalType::VARCHAR;
	table_function.named_parameters["date_col"] = LogicalType::VARCHAR;
	table_function.named_parameters["value_col"] = LogicalType::VARCHAR;
	table_function.named_parameters["frequency"] = LogicalType::ANY;

	return make_uniq<TableFunction>(std::move(table_function));
}

} // namespace duckdb
