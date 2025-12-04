#include "ts_fill_gaps_function.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/catalog/catalog_entry/duck_table_entry.hpp"
#include "duckdb/parser/qualified_name.hpp"
#include "duckdb/planner/binder.hpp"
#include "duckdb/parser/tableref/table_function_ref.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/storage/data_table.hpp"
#include "duckdb/storage/table/scan_state.hpp"
#include "duckdb/transaction/transaction.hpp"
#include "duckdb/transaction/duck_transaction.hpp"

// No forward declarations needed - all types are complete
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/numeric_utils.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace duckdb {

namespace ts_fill_gaps_internal {

// Validate frequency compatibility with date column type
void ValidateFrequencyCompatibility(const LogicalType &date_col_type, const FrequencyConfig &frequency) {
	if (date_col_type.id() == LogicalTypeId::DATE) {
		// DATE columns: Only allow day-level or larger intervals
		if (frequency.type == FrequencyType::INTEGER_STEP) {
			throw InvalidInputException(
			    "DATE column cannot use INTEGER frequency. Use VARCHAR frequency like '1d', '1w', etc.");
		}
		// Check if interval is sub-day (30m, 1h)
		if (frequency.type == FrequencyType::VARCHAR_INTERVAL) {
			auto hours = std::chrono::duration_cast<std::chrono::hours>(frequency.interval).count();
			auto minutes = std::chrono::duration_cast<std::chrono::minutes>(frequency.interval).count();
			if (hours < 24 && minutes < 1440) { // Less than 24 hours
				throw InvalidInputException(
				    "DATE column cannot use sub-day intervals like '30m' or '1h'. Use '1d' or larger intervals.");
			}
		}
	} else if (date_col_type.id() == LogicalTypeId::INTEGER || date_col_type.id() == LogicalTypeId::BIGINT) {
		// INTEGER/BIGINT columns: Only allow INTEGER frequency
		if (frequency.type == FrequencyType::VARCHAR_INTERVAL) {
			throw InvalidInputException(
			    "INTEGER/BIGINT date column can only use INTEGER frequency, not VARCHAR intervals.");
		}
	}
	// TIMESTAMP columns: Allow both VARCHAR and INTEGER frequencies (no restrictions)
}

// Parse interval string to chrono duration
std::chrono::system_clock::duration ParseIntervalString(const std::string &freq_str) {
	std::string upper = StringUtil::Upper(freq_str);
	StringUtil::Trim(upper);

	if (upper == "1D" || upper == "1DAY") {
		return std::chrono::hours(24);
	} else if (upper == "30M" || upper == "30MIN" || upper == "30MINUTE" || upper == "30MINUTES") {
		return std::chrono::minutes(30);
	} else if (upper == "1H" || upper == "1HOUR" || upper == "1HOURS") {
		return std::chrono::hours(1);
	} else if (upper == "1W" || upper == "1WEEK" || upper == "1WEEKS") {
		return std::chrono::hours(24 * 7);
	} else if (upper == "1MO" || upper == "1MONTH" || upper == "1MONTHS") {
		// Approximate month as 30 days
		return std::chrono::hours(24 * 30);
	} else if (upper == "1Q" || upper == "1QUARTER" || upper == "1QUARTERS") {
		// Approximate quarter as 90 days
		return std::chrono::hours(24 * 90);
	} else if (upper == "1Y" || upper == "1YEAR" || upper == "1YEARS") {
		// Approximate year as 365 days
		return std::chrono::hours(24 * 365);
	} else {
		// Default to 1 day
		return std::chrono::hours(24);
	}
}

// Parse frequency string or integer to FrequencyConfig
FrequencyConfig ParseFrequency(const Value &frequency_value, const LogicalType &date_col_type) {
	FrequencyConfig config;
	config.original_value = frequency_value.ToString();

	if (frequency_value.IsNull()) {
		throw InvalidInputException("frequency parameter is required and cannot be NULL");
	}

	if (frequency_value.type().id() == LogicalTypeId::VARCHAR) {
		config.type = FrequencyType::VARCHAR_INTERVAL;
		std::string freq_str = frequency_value.GetValue<string>();
		if (freq_str.empty()) {
			throw InvalidInputException("frequency parameter cannot be empty");
		}
		config.interval = ParseIntervalString(freq_str);
	} else if (frequency_value.type().id() == LogicalTypeId::INTEGER ||
	           frequency_value.type().id() == LogicalTypeId::BIGINT) {
		config.type = FrequencyType::INTEGER_STEP;
		config.step = frequency_value.GetValue<int64_t>();
		if (config.step <= 0) {
			throw InvalidInputException("INTEGER frequency must be positive");
		}
	} else {
		throw InvalidInputException("frequency must be VARCHAR or INTEGER/BIGINT");
	}

	// Validate compatibility
	ValidateFrequencyCompatibility(date_col_type, config);

	return config;
}

// Generate date range for VARCHAR frequency
std::vector<std::chrono::system_clock::time_point> GenerateDateRange(std::chrono::system_clock::time_point min_date,
                                                                     std::chrono::system_clock::time_point max_date,
                                                                     std::chrono::system_clock::duration interval) {
	std::vector<std::chrono::system_clock::time_point> result;
	if (min_date > max_date) {
		return result; // Empty range
	}

	std::chrono::system_clock::time_point current = min_date;
	while (current <= max_date) {
		result.push_back(current);
		current += interval;
	}

	return result;
}

// Generate integer range for INTEGER frequency
std::vector<int64_t> GenerateIntegerRange(int64_t min_val, int64_t max_val, int64_t step) {
	std::vector<int64_t> result;
	if (min_val > max_val) {
		return result; // Empty range
	}

	int64_t current = min_val;
	while (current <= max_val) {
		result.push_back(current);
		current += step;
	}

	return result;
}

} // namespace ts_fill_gaps_internal

// Helper functions for type conversion
namespace ts_fill_gaps_internal {

// Convert DuckDB DATE to std::chrono::system_clock::time_point
std::chrono::system_clock::time_point DateToTimePoint(const date_t &date) {
	// DATE is stored as days since 1970-01-01
	// Convert to microseconds since epoch
	int64_t days = date.days;
	int64_t micros = days * 24LL * 60LL * 60LL * 1000000LL; // days to microseconds
	std::chrono::microseconds duration(micros);
	return std::chrono::system_clock::time_point(duration);
}

// Convert DuckDB TIMESTAMP to std::chrono::system_clock::time_point
std::chrono::system_clock::time_point TimestampToTimePoint(const timestamp_t &ts) {
	std::chrono::microseconds duration(ts.value);
	return std::chrono::system_clock::time_point(duration);
}

// Convert std::chrono::system_clock::time_point to DuckDB TIMESTAMP
timestamp_t TimePointToTimestamp(const std::chrono::system_clock::time_point &tp) {
	auto duration = tp.time_since_epoch();
	auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
	return timestamp_t(micros);
}

// Convert std::chrono::system_clock::time_point to DuckDB DATE
date_t TimePointToDate(const std::chrono::system_clock::time_point &tp) {
	auto duration = tp.time_since_epoch();
	auto micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
	int64_t days = micros / (24LL * 60LL * 60LL * 1000000LL);
	return date_t(UnsafeNumericCast<int32_t>(days));
}

} // namespace ts_fill_gaps_internal

// Helper to extract table name (similar to eda_bind_replace.cpp)
static string ExtractTableName(const TableFunctionBindInput &input, idx_t param_idx, const Value &fallback_value) {
	// Try to extract from original expression in ref before it was evaluated
	if (input.ref.function && input.ref.function->GetExpressionType() == ExpressionType::FUNCTION) {
		auto &fexpr = input.ref.function->Cast<FunctionExpression>();
		if (param_idx < fexpr.children.size()) {
			auto &expr = fexpr.children[param_idx];
			if (expr->GetExpressionType() == ExpressionType::COLUMN_REF) {
				// Table name passed as identifier
				auto &colref = expr->Cast<ColumnRefExpression>();
				return colref.GetColumnName();
			} else if (expr->GetExpressionType() == ExpressionType::VALUE_CONSTANT) {
				// Table name passed as string literal
				return expr->Cast<ConstantExpression>().value.ToString();
			}
		}
	}
	// Fallback: try input_table_names if available (for TABLE type parameters)
	if (param_idx < input.input_table_names.size() && !input.input_table_names[param_idx].empty()) {
		return input.input_table_names[param_idx];
	}
	// Final fallback to evaluated value
	return fallback_value.ToString();
}

// Helper to extract column name
static string ExtractColumnName(const TableFunctionBindInput &input, idx_t param_idx, const Value &fallback_value) {
	// Try to extract from original expression in ref before it was evaluated
	if (input.ref.function && input.ref.function->GetExpressionType() == ExpressionType::FUNCTION) {
		auto &fexpr = input.ref.function->Cast<FunctionExpression>();
		if (param_idx < fexpr.children.size()) {
			auto &expr = fexpr.children[param_idx];
			if (expr->GetExpressionType() == ExpressionType::COLUMN_REF) {
				auto &colref = expr->Cast<ColumnRefExpression>();
				return colref.GetColumnName();
			} else if (expr->GetExpressionType() == ExpressionType::VALUE_CONSTANT) {
				return expr->Cast<ConstantExpression>().value.ToString();
			}
		}
	}
	// Fallback to evaluated value
	return fallback_value.ToString();
}

// Bind function for Table-In-Out operator (internal function)
// This function receives TABLE input via input_table_types/input_table_names
unique_ptr<FunctionData> TSFillGapsOperatorBind(ClientContext &context, TableFunctionBindInput &input,
                                                vector<LogicalType> &return_types, vector<string> &names) {
	// For table-in-out function, parameters are:
	// - Input table columns come from input_table_types/input_table_names automatically
	// - Function arguments are: group_col, date_col, value_col, frequency

	// Validate parameter count (4 arguments: group_col, date_col, value_col, frequency)
	if (input.inputs.size() < 4) {
		throw InvalidInputException(
		    "anofox_fcst_ts_fill_gaps_operator requires 4 arguments: group_col, date_col, value_col, frequency");
	}

	// Get input table schema from table-in-out input
	if (input.input_table_types.empty() || input.input_table_names.empty()) {
		throw InvalidInputException("anofox_fcst_ts_fill_gaps_operator requires TABLE input");
	}

	vector<LogicalType> table_types = input.input_table_types;
	vector<string> table_names = input.input_table_names;

	// Extract parameters
	string group_col = ExtractColumnName(input, 0, input.inputs[0]);
	string date_col = ExtractColumnName(input, 1, input.inputs[1]);
	string value_col = ExtractColumnName(input, 2, input.inputs[2]);
	Value frequency_value = input.inputs[3];

	// Find column indices
	idx_t group_col_idx = DConstants::INVALID_INDEX;
	idx_t date_col_idx = DConstants::INVALID_INDEX;
	idx_t value_col_idx = DConstants::INVALID_INDEX;

	for (idx_t i = 0; i < table_names.size(); i++) {
		if (table_names[i] == group_col) {
			group_col_idx = i;
		}
		if (table_names[i] == date_col) {
			date_col_idx = i;
		}
		if (table_names[i] == value_col) {
			value_col_idx = i;
		}
	}

	if (group_col_idx == DConstants::INVALID_INDEX) {
		throw InvalidInputException("Column '" + group_col + "' not found in input table");
	}
	if (date_col_idx == DConstants::INVALID_INDEX) {
		throw InvalidInputException("Column '" + date_col + "' not found in input table");
	}
	if (value_col_idx == DConstants::INVALID_INDEX) {
		throw InvalidInputException("Column '" + value_col + "' not found in input table");
	}

	// Get date column type
	LogicalType date_col_type = table_types[date_col_idx];

	// Parse and validate frequency
	auto frequency = ts_fill_gaps_internal::ParseFrequency(frequency_value, date_col_type);

	// Create bind data
	auto bind_data = make_uniq<TSFillGapsBindData>();
	bind_data->group_col = group_col;
	bind_data->date_col = date_col;
	bind_data->value_col = value_col;
	bind_data->frequency = frequency;
	bind_data->group_col_idx = group_col_idx;
	bind_data->date_col_idx = date_col_idx;
	bind_data->value_col_idx = value_col_idx;
	bind_data->date_col_type = date_col_type;

	// Set return types: preserve all input columns, same types and names
	return_types = table_types;
	names = table_names;

	// Store in bind_data for later use
	bind_data->return_types = table_types;
	bind_data->return_names = table_names;

	return std::move(bind_data);
}

// Global state initialization for Table-In-Out operator
unique_ptr<GlobalTableFunctionState> TSFillGapsOperatorInitGlobal(ClientContext &context,
                                                                  TableFunctionInitInput &input) {
	auto global_state = make_uniq<TSFillGapsGlobalState>();
	auto &bind_data = input.bind_data->Cast<TSFillGapsBindData>();
	global_state->bind_data = &bind_data;
	global_state->group_col_idx = bind_data.group_col_idx;
	global_state->date_col_idx = bind_data.date_col_idx;
	global_state->value_col_idx = bind_data.value_col_idx;

	// No table scanning needed - DuckDB pipes data to us via input DataChunk
	return std::move(global_state);
}

// Local state initialization for Table-In-Out operator
unique_ptr<LocalTableFunctionState> TSFillGapsOperatorInitLocal(ExecutionContext &context,
                                                                TableFunctionInitInput &input,
                                                                GlobalTableFunctionState *global_state) {
	auto local_state = make_uniq<TSFillGapsLocalState>();
	auto &gstate = global_state->Cast<TSFillGapsGlobalState>();

	local_state->input_done = false;
	local_state->output_offset = 0;
	local_state->current_group_index = 0;
	local_state->current_date_index = 0;
	local_state->is_integer_date_mode = (gstate.bind_data->date_col_type.id() == LogicalTypeId::INTEGER ||
	                                     gstate.bind_data->date_col_type.id() == LogicalTypeId::BIGINT);

	return std::move(local_state);
}

// Helper to extract group key as string from Value
static string GetGroupKey(const Value &group_value) {
	if (group_value.IsNull()) {
		return "__NULL__";
	}
	return group_value.ToString();
}

// Helper to convert DuckDB date/timestamp to time_point
static std::chrono::system_clock::time_point ConvertToTimePoint(const Value &date_value, const LogicalType &date_type) {
	using namespace ts_fill_gaps_internal;

	if (date_value.IsNull()) {
		throw InvalidInputException("Date column contains NULL values - cannot process");
	}

	if (date_type.id() == LogicalTypeId::DATE) {
		auto date = date_value.GetValue<date_t>();
		return DateToTimePoint(date);
	} else if (date_type.id() == LogicalTypeId::TIMESTAMP) {
		auto timestamp = date_value.GetValue<timestamp_t>();
		return TimestampToTimePoint(timestamp);
	} else {
		throw InvalidInputException("Unsupported date column type for time_point conversion");
	}
}

// Table-in-out function: accumulates input data per group
// Called repeatedly with input chunks until all input is processed
OperatorResultType TSFillGapsOperatorInOut(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                           DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<TSFillGapsBindData>();
	auto &gstate = data_p.global_state->Cast<TSFillGapsGlobalState>();
	auto &lstate = data_p.local_state->Cast<TSFillGapsLocalState>();

	// Accumulate input data per group
	if (input.size() > 0) {
		// Process input chunk: accumulate data per group
		for (idx_t i = 0; i < input.size(); i++) {
			// Extract group, date, value
			auto group_val = input.data[gstate.group_col_idx].GetValue(i);
			auto date_val = input.data[gstate.date_col_idx].GetValue(i);
			auto value_val = input.data[gstate.value_col_idx].GetValue(i);

			if (date_val.IsNull()) {
				continue; // Skip rows with NULL dates
			}

			string group_key = GetGroupKey(group_val);

			// Get or create series data for this group
			if (lstate.series_data.find(group_key) == lstate.series_data.end()) {
				lstate.series_data[group_key] = SeriesData();
				lstate.series_data[group_key].is_integer_date = lstate.is_integer_date_mode;
				lstate.current_group_order.push_back(group_key);
				lstate.group_value_map[group_key] = group_val;
			}

			auto &series = lstate.series_data[group_key];

			// Store date
			if (lstate.is_integer_date_mode) {
				int64_t int_date = date_val.GetValue<int64_t>();
				series.integer_dates.push_back(int_date);
				series.integer_date_set.insert(int_date);
			} else {
				auto time_point = ConvertToTimePoint(date_val, bind_data.date_col_type);
				series.timestamps.push_back(time_point);
				series.timestamp_set.insert(time_point);
			}

			// Store value (with NULL handling)
			if (value_val.IsNull()) {
				series.values.push_back(std::numeric_limits<double>::quiet_NaN()); // Use NaN as NULL marker
			} else {
				series.values.push_back(value_val.GetValue<double>());
			}

			// Store other columns (for column preservation)
			std::vector<Value> other_cols;
			for (idx_t col_idx = 0; col_idx < input.ColumnCount(); col_idx++) {
				if (col_idx != gstate.group_col_idx && col_idx != gstate.date_col_idx &&
				    col_idx != gstate.value_col_idx) {
					other_cols.push_back(input.data[col_idx].GetValue(i));
				}
			}
			series.other_columns.push_back(other_cols);
			series.group_values.push_back(group_val);
		}
	}

	// Signal we need more input (we'll process in the finalize function)
	output.SetCardinality(0);
	return OperatorResultType::NEED_MORE_INPUT;
}

// Finalize function: generates gap-filled output
// Called when all input is processed
OperatorFinalizeResultType TSFillGapsOperatorFinal(ExecutionContext &context, TableFunctionInput &data_p,
                                                   DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<TSFillGapsBindData>();
	auto &gstate = data_p.global_state->Cast<TSFillGapsGlobalState>();
	auto &lstate = data_p.local_state->Cast<TSFillGapsLocalState>();

	// Generate date ranges for all groups if not already done
	if (!lstate.input_done) {
		lstate.input_done = true;

		// Prepare output generation: determine group order and generate date ranges
		for (const auto &group_key : lstate.current_group_order) {
			auto &series = lstate.series_data[group_key];

			if (series.timestamps.empty() && series.integer_dates.empty()) {
				continue; // Skip empty groups
			}

			// Determine min/max and generate date range (store separately from original data)
			if (lstate.is_integer_date_mode) {
				if (!series.integer_dates.empty()) {
					auto min_max = std::minmax_element(series.integer_dates.begin(), series.integer_dates.end());
					series.generated_integer_dates = ts_fill_gaps_internal::GenerateIntegerRange(
					    *min_max.first, *min_max.second, bind_data.frequency.step);

					// Build lookup map: generated date -> index in original data
					for (idx_t i = 0; i < series.integer_dates.size(); i++) {
						series.integer_date_to_index[series.integer_dates[i]] = i;
					}
				}
			} else {
				if (!series.timestamps.empty()) {
					auto min_max = std::minmax_element(series.timestamps.begin(), series.timestamps.end());
					series.generated_timestamps = ts_fill_gaps_internal::GenerateDateRange(
					    *min_max.first, *min_max.second, bind_data.frequency.interval);
				}
			}
		}

		// Reset output state
		lstate.current_group_index = 0;
		lstate.current_date_index = 0;
		if (!lstate.current_group_order.empty()) {
			lstate.current_group_key = lstate.current_group_order[0];
		}
	}

	// Generate output rows
	idx_t output_count = 0;
	output.SetCardinality(0);

	// Initialize output DataChunk with return types from bind_data
	if (output.ColumnCount() == 0) {
		output.InitializeEmpty(bind_data.return_types);
	}

	// Initialize output columns
	for (idx_t col_idx = 0; col_idx < output.ColumnCount(); col_idx++) {
		output.data[col_idx].SetVectorType(VectorType::FLAT_VECTOR);
	}

	// Process groups and dates
	while (output_count < STANDARD_VECTOR_SIZE && lstate.current_group_index < lstate.current_group_order.size()) {
		const string &group_key = lstate.current_group_order[lstate.current_group_index];
		auto &series = lstate.series_data[group_key];

		// Get current date range for this group
		idx_t range_size =
		    lstate.is_integer_date_mode ? series.generated_integer_dates.size() : series.generated_timestamps.size();

		if (lstate.current_date_index >= range_size) {
			// Finished this group, move to next
			lstate.current_group_index++;
			lstate.current_date_index = 0;
			if (lstate.current_group_index < lstate.current_group_order.size()) {
				lstate.current_group_key = lstate.current_group_order[lstate.current_group_index];
			}
			continue;
		}

		// Get current date
		Value date_value;
		if (lstate.is_integer_date_mode) {
			int64_t int_date = series.generated_integer_dates[lstate.current_date_index];
			date_value = Value::BIGINT(int_date);

			// Check if this date exists in original data
			bool date_exists = series.integer_date_set.find(int_date) != series.integer_date_set.end();
			idx_t original_index = date_exists ? series.integer_date_to_index[int_date] : DConstants::INVALID_INDEX;

			// Emit row - preserve all columns in original order
			idx_t col_idx = 0;
			idx_t other_col_offset = 0; // Track position in other_columns vector

			for (idx_t orig_col = 0; orig_col < bind_data.return_types.size(); orig_col++) {
				if (orig_col == gstate.group_col_idx) {
					// Group column
					output.SetValue(col_idx++, output_count, lstate.group_value_map[group_key]);
				} else if (orig_col == gstate.date_col_idx) {
					// Date column
					output.SetValue(col_idx++, output_count, date_value);
				} else if (orig_col == gstate.value_col_idx) {
					// Value column - NULL if gap, original value if exists
					if (date_exists && original_index < series.values.size()) {
						double val = series.values[original_index];
						if (std::isnan(val)) {
							output.SetValue(col_idx++, output_count, Value()); // NULL
						} else {
							output.SetValue(col_idx++, output_count, Value::DOUBLE(val));
						}
					} else {
						output.SetValue(col_idx++, output_count, Value()); // NULL for gaps
					}
				} else {
					// Other columns - preserve from original data if exists, NULL for gaps
					if (date_exists && original_index < series.other_columns.size() &&
					    other_col_offset < series.other_columns[original_index].size()) {
						output.SetValue(col_idx++, output_count,
						                series.other_columns[original_index][other_col_offset++]);
					} else {
						output.SetValue(col_idx++, output_count, Value()); // NULL for gaps
						other_col_offset++;                                // Advance offset even for gaps
					}
				}
			}
		} else {
			// TIMESTAMP/DATE mode
			auto time_point = series.generated_timestamps[lstate.current_date_index];
			bool date_exists = series.timestamp_set.find(time_point) != series.timestamp_set.end();

			// Find original index
			idx_t original_index = DConstants::INVALID_INDEX;
			if (date_exists) {
				for (idx_t i = 0; i < series.timestamps.size(); i++) {
					if (series.timestamps[i] == time_point) {
						original_index = i;
						break;
					}
				}
			}

			// Convert time_point back to DuckDB type
			if (bind_data.date_col_type.id() == LogicalTypeId::DATE) {
				date_value = Value::DATE(ts_fill_gaps_internal::TimePointToDate(time_point));
			} else {
				date_value = Value::TIMESTAMP(ts_fill_gaps_internal::TimePointToTimestamp(time_point));
			}

			// Emit row (same logic as integer mode)
			idx_t col_idx = 0;
			idx_t other_col_offset = 0; // Track position in other_columns vector

			for (idx_t orig_col = 0; orig_col < bind_data.return_types.size(); orig_col++) {
				if (orig_col == gstate.group_col_idx) {
					output.SetValue(col_idx++, output_count, lstate.group_value_map[group_key]);
				} else if (orig_col == gstate.date_col_idx) {
					output.SetValue(col_idx++, output_count, date_value);
				} else if (orig_col == gstate.value_col_idx) {
					if (date_exists && original_index < series.values.size()) {
						double val = series.values[original_index];
						if (std::isnan(val)) {
							output.SetValue(col_idx++, output_count, Value()); // NULL
						} else {
							output.SetValue(col_idx++, output_count, Value::DOUBLE(val));
						}
					} else {
						output.SetValue(col_idx++, output_count, Value()); // NULL for gaps
					}
				} else {
					// Other columns - preserve from original data if exists, NULL for gaps
					if (date_exists && original_index < series.other_columns.size() &&
					    other_col_offset < series.other_columns[original_index].size()) {
						output.SetValue(col_idx++, output_count,
						                series.other_columns[original_index][other_col_offset++]);
					} else {
						output.SetValue(col_idx++, output_count, Value()); // NULL for gaps
						other_col_offset++;                                // Advance offset even for gaps
					}
				}
			}
		}

		output_count++;
		lstate.current_date_index++;
	}

	output.SetCardinality(output_count);

	// Check if we're done
	if (lstate.current_group_index >= lstate.current_group_order.size()) {
		return OperatorFinalizeResultType::FINISHED;
	}
	return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// Cardinality estimation
unique_ptr<NodeStatistics> TSFillGapsCardinality(ClientContext &context, const FunctionData *bind_data) {
	// For table-in-out functions, cardinality is typically unknown until all input is processed
	// Return nullptr to let DuckDB estimate
	return nullptr;
}

// Create table-in-out function (internal operator)
// This function takes TABLE input and processes it
// Mark as used to prevent linker from dropping it during dead code elimination
__attribute__((used)) unique_ptr<TableFunction> CreateTSFillGapsOperatorTableFunction() {
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
