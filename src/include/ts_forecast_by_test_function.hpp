#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace duckdb {

// Bind data for TS_FORECAST_BY_TEST function (Table-In-Out version)
struct TSForecastByTestBindData : public TableFunctionData {
	// Column configuration
	std::string group_col;
	std::string date_col;
	std::string target_col;

	// Column indices (resolved at bind time)
	idx_t group_col_idx = DConstants::INVALID_INDEX;
	idx_t date_col_idx = DConstants::INVALID_INDEX;
	idx_t target_col_idx = DConstants::INVALID_INDEX;

	// Column types
	LogicalType group_col_type;
	LogicalType date_col_type;

	// Forecast parameters
	int32_t horizon = 1;
	int32_t seasonal_period = 0; // For AutoARIMA
	double confidence_level = 0.90;
	bool insample_forecast = false; // For future use

	// Dynamic column names based on confidence level
	std::string lower_col_name = "lower_90";
	std::string upper_col_name = "upper_90";

	// Return schema
	vector<LogicalType> return_types;
	vector<string> return_names;

	TSForecastByTestBindData() = default;
};

// Global state for TS_FORECAST_BY_TEST
struct TSForecastByTestGlobalState : public GlobalTableFunctionState {
	const TSForecastByTestBindData *bind_data = nullptr;
};

// Data point for accumulation
struct ForecastDataPoint {
	std::chrono::system_clock::time_point timestamp;
	double value;
	Value original_date_val; // Preserve original date type for output
};

// Group data container
struct ForecastGroupData {
	std::vector<ForecastDataPoint> points;
	Value group_value; // Original group value for output
};

// Local state for table-in-out processing
struct TSForecastByTestLocalState : public LocalTableFunctionState {
	// Accumulated data per group
	std::unordered_map<std::string, ForecastGroupData> groups;
	std::vector<std::string> group_order; // Maintain processing order

	// Processing flags
	bool input_done = false;

	// Output iteration state
	idx_t current_group_idx = 0;
	idx_t current_row_idx = 0;

	// Current group's processed results
	struct ProcessedForecast {
		std::vector<int32_t> forecast_steps;
		std::vector<Value> forecast_timestamps;
		std::vector<double> point_forecasts;
		std::vector<double> lower_bounds;
		std::vector<double> upper_bounds;
		std::string model_name;
	} current_forecast;

	TSForecastByTestLocalState() = default;
};

// Helper namespace for date conversion
namespace forecast_by_test_internal {

std::chrono::system_clock::time_point DateToTimePoint(const date_t &date);
std::chrono::system_clock::time_point TimestampToTimePoint(const timestamp_t &ts);
std::chrono::system_clock::time_point ConvertToTimePoint(const Value &date_value, const LogicalType &date_type);

} // namespace forecast_by_test_internal

// Function declarations for Table-In-Out pattern
unique_ptr<FunctionData> TSForecastByTestOperatorBind(ClientContext &context, TableFunctionBindInput &input,
                                                      vector<LogicalType> &return_types, vector<string> &names);

unique_ptr<GlobalTableFunctionState> TSForecastByTestInitGlobal(ClientContext &context, TableFunctionInitInput &input);

unique_ptr<LocalTableFunctionState> TSForecastByTestInitLocal(ExecutionContext &context, TableFunctionInitInput &input,
                                                              GlobalTableFunctionState *global_state);

OperatorResultType TSForecastByTestOperatorInOut(ExecutionContext &context, TableFunctionInput &data_p,
                                                 DataChunk &input, DataChunk &output);

OperatorFinalizeResultType TSForecastByTestOperatorFinal(ExecutionContext &context, TableFunctionInput &data_p,
                                                         DataChunk &output);

unique_ptr<NodeStatistics> TSForecastByTestCardinality(ClientContext &context, const FunctionData *bind_data);

unique_ptr<TableRef> TSForecastByTestBindReplace(ClientContext &context, TableFunctionBindInput &input);

// Registration function
void RegisterTSForecastByTestFunction(ExtensionLoader &loader);

} // namespace duckdb
