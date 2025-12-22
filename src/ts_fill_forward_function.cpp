#include "ts_fill_forward_function.hpp"
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
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/numeric_utils.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cmath>

namespace duckdb {

// Helper to extract column name (same as ts_fill_gaps)
static string ExtractColumnName(const TableFunctionBindInput &input, idx_t param_idx, const Value &fallback_value) {
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
	return fallback_value.ToString();
}

// Helper functions for type conversion (same as in ts_fill_gaps_function.cpp)
namespace ts_fill_forward_internal {

// Convert DuckDB DATE to std::chrono::system_clock::time_point
std::chrono::system_clock::time_point DateToTimePoint(const date_t &date) {
	int64_t days = date.days;
	int64_t micros = days * 24LL * 60LL * 60LL * 1000000LL;
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

} // namespace ts_fill_forward_internal

// Helper to convert target_date Value to time_point
static std::chrono::system_clock::time_point ConvertTargetDateToTimePoint(const Value &target_date_value,
                                                                          const LogicalType &date_col_type) {
	if (target_date_value.IsNull()) {
		throw InvalidInputException("target_date parameter is required and cannot be NULL");
	}

	// Try to get the value directly based on the input type
	auto target_type = target_date_value.type();

	if (date_col_type.id() == LogicalTypeId::DATE) {
		// For DATE columns, try to extract as DATE
		if (target_type.id() == LogicalTypeId::DATE) {
			auto date = target_date_value.GetValue<date_t>();
			return ts_fill_forward_internal::DateToTimePoint(date);
		} else if (target_type.id() == LogicalTypeId::TIMESTAMP) {
			// Convert TIMESTAMP to DATE by extracting date part
			auto ts = target_date_value.GetValue<timestamp_t>();
			int64_t micros = ts.value;
			int64_t days = micros / (24LL * 60LL * 60LL * 1000000LL);
			date_t date(UnsafeNumericCast<int32_t>(days));
			return ts_fill_forward_internal::DateToTimePoint(date);
		} else if (target_type.id() == LogicalTypeId::VARCHAR) {
			// Try to parse as DATE string - use DuckDB's casting
			auto str = target_date_value.GetValue<string>();
			auto date = Date::FromString(str);
			return ts_fill_forward_internal::DateToTimePoint(date);
		}
		throw InvalidInputException("target_date must be convertible to DATE for DATE columns");
	} else if (date_col_type.id() == LogicalTypeId::TIMESTAMP) {
		// For TIMESTAMP columns, try to extract as TIMESTAMP
		if (target_type.id() == LogicalTypeId::TIMESTAMP) {
			auto timestamp = target_date_value.GetValue<timestamp_t>();
			return ts_fill_forward_internal::TimestampToTimePoint(timestamp);
		} else if (target_type.id() == LogicalTypeId::DATE) {
			// Convert DATE to TIMESTAMP
			auto date = target_date_value.GetValue<date_t>();
			int64_t days = date.days;
			int64_t micros = days * 24LL * 60LL * 60LL * 1000000LL;
			timestamp_t ts(micros);
			return ts_fill_forward_internal::TimestampToTimePoint(ts);
		} else if (target_type.id() == LogicalTypeId::VARCHAR) {
			// Try to parse as TIMESTAMP string
			auto str = target_date_value.GetValue<string>();
			auto ts = Timestamp::FromString(str, false); // false = don't use offset
			return ts_fill_forward_internal::TimestampToTimePoint(ts);
		}
		throw InvalidInputException("target_date must be convertible to TIMESTAMP for TIMESTAMP columns");
	} else {
		throw InvalidInputException("ConvertTargetDateToTimePoint called with non-date type");
	}
}

// Helper to convert target_date Value to integer
static int64_t ConvertTargetDateToInteger(const Value &target_date_value) {
	if (target_date_value.IsNull()) {
		throw InvalidInputException("target_date parameter is required and cannot be NULL");
	}

	auto target_type = target_date_value.type();
	if (target_type.id() == LogicalTypeId::INTEGER) {
		return target_date_value.GetValue<int32_t>();
	} else if (target_type.id() == LogicalTypeId::BIGINT) {
		return target_date_value.GetValue<int64_t>();
	} else if (target_type.id() == LogicalTypeId::VARCHAR) {
		// Try to parse as integer
		auto str = target_date_value.GetValue<string>();
		return std::stoll(str);
	}
	throw InvalidInputException("target_date must be INTEGER for INTEGER date columns");
}

// Bind function for Table-In-Out operator (internal function)
unique_ptr<FunctionData> TSFillForwardOperatorBind(ClientContext &context, TableFunctionBindInput &input,
                                                   vector<LogicalType> &return_types, vector<string> &names) {
	// For table-in-out function, parameters are:
	// - Input table columns come from input_table_types/input_table_names automatically
	// - Function arguments are: group_col, date_col, value_col, target_date, frequency

	// Validate parameter count (5 arguments)
	if (input.inputs.size() < 5) {
		throw InvalidInputException("anofox_fcst_ts_fill_forward_operator requires 5 arguments: group_col, date_col, "
		                            "value_col, target_date, frequency");
	}

	// Get input table schema from table-in-out input
	if (input.input_table_types.empty() || input.input_table_names.empty()) {
		throw InvalidInputException("anofox_fcst_ts_fill_forward_operator requires TABLE input");
	}

	vector<LogicalType> table_types = input.input_table_types;
	vector<string> table_names = input.input_table_names;

	// Extract parameters
	string group_col = ExtractColumnName(input, 0, input.inputs[0]);
	string date_col = ExtractColumnName(input, 1, input.inputs[1]);
	string value_col = ExtractColumnName(input, 2, input.inputs[2]);
	Value target_date_value = input.inputs[3];
	Value frequency_value = input.inputs[4];

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

	// Validate target_date is not NULL
	if (target_date_value.IsNull()) {
		throw InvalidInputException("target_date parameter is required and cannot be NULL");
	}

	// Parse and validate frequency
	auto frequency = ts_fill_gaps_internal::ParseFrequency(frequency_value, date_col_type);

	// Create bind data
	auto bind_data = make_uniq<TSFillForwardBindData>();
	bind_data->group_col = group_col;
	bind_data->date_col = date_col;
	bind_data->value_col = value_col;
	bind_data->frequency = frequency;
	bind_data->target_date_value = target_date_value;
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
unique_ptr<GlobalTableFunctionState> TSFillForwardOperatorInitGlobal(ClientContext &context,
                                                                     TableFunctionInitInput &input) {
	auto global_state = make_uniq<TSFillForwardGlobalState>();
	auto &bind_data = input.bind_data->Cast<TSFillForwardBindData>();
	global_state->bind_data = &bind_data;
	global_state->group_col_idx = bind_data.group_col_idx;
	global_state->date_col_idx = bind_data.date_col_idx;
	global_state->value_col_idx = bind_data.value_col_idx;

	return std::move(global_state);
}

// Local state initialization for Table-In-Out operator
unique_ptr<LocalTableFunctionState> TSFillForwardOperatorInitLocal(ExecutionContext &context,
                                                                   TableFunctionInitInput &input,
                                                                   GlobalTableFunctionState *global_state) {
	auto local_state = make_uniq<TSFillForwardLocalState>();
	auto &gstate = global_state->Cast<TSFillForwardGlobalState>();

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
	if (date_value.IsNull()) {
		throw InvalidInputException("Date column contains NULL values - cannot process");
	}

	if (date_type.id() == LogicalTypeId::DATE) {
		auto date = date_value.GetValue<date_t>();
		return ts_fill_forward_internal::DateToTimePoint(date);
	} else if (date_type.id() == LogicalTypeId::TIMESTAMP) {
		auto timestamp = date_value.GetValue<timestamp_t>();
		return ts_fill_forward_internal::TimestampToTimePoint(timestamp);
	} else {
		throw InvalidInputException("Unsupported date column type for time_point conversion");
	}
}

// Table-in-out function: accumulates input data per group
OperatorResultType TSFillForwardOperatorInOut(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                              DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<TSFillForwardBindData>();
	auto &gstate = data_p.global_state->Cast<TSFillForwardGlobalState>();
	auto &lstate = data_p.local_state->Cast<TSFillForwardLocalState>();

	// Accumulate input data per group
	if (input.size() > 0) {
		for (idx_t i = 0; i < input.size(); i++) {
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

// Finalize function: generates forward-filled output
OperatorFinalizeResultType TSFillForwardOperatorFinal(ExecutionContext &context, TableFunctionInput &data_p,
                                                      DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<TSFillForwardBindData>();
	auto &gstate = data_p.global_state->Cast<TSFillForwardGlobalState>();
	auto &lstate = data_p.local_state->Cast<TSFillForwardLocalState>();

	// Generate date ranges for all groups if not already done
	if (!lstate.input_done) {
		lstate.input_done = true;

		// Prepare output generation: determine group order and generate date ranges
		for (const auto &group_key : lstate.current_group_order) {
			auto &series = lstate.series_data[group_key];

			if (series.timestamps.empty() && series.integer_dates.empty()) {
				continue; // Skip empty groups
			}

			// KEY DIFFERENCE FROM ts_fill_gaps:
			// Generate range from min(date) to target_date (not max(date))
			if (lstate.is_integer_date_mode) {
				if (!series.integer_dates.empty()) {
					int64_t min_date = *std::min_element(series.integer_dates.begin(), series.integer_dates.end());
					int64_t target_date = ConvertTargetDateToInteger(bind_data.target_date_value);

					// If target_date is before min_date, just use original data
					if (target_date < min_date) {
						series.generated_integer_dates = series.integer_dates;
						std::sort(series.generated_integer_dates.begin(), series.generated_integer_dates.end());
					} else {
						series.generated_integer_dates = ts_fill_gaps_internal::GenerateIntegerRange(
						    min_date, target_date, bind_data.frequency.step);
					}

					// Build lookup map: generated date -> index in original data
					for (idx_t i = 0; i < series.integer_dates.size(); i++) {
						series.integer_date_to_index[series.integer_dates[i]] = i;
					}
				}
			} else {
				if (!series.timestamps.empty()) {
					auto min_date = *std::min_element(series.timestamps.begin(), series.timestamps.end());
					auto target_date =
					    ConvertTargetDateToTimePoint(bind_data.target_date_value, bind_data.date_col_type);

					// If target_date is before min_date, just use original data
					if (target_date < min_date) {
						series.generated_timestamps = series.timestamps;
						std::sort(series.generated_timestamps.begin(), series.generated_timestamps.end());
					} else {
						series.generated_timestamps = ts_fill_gaps_internal::GenerateDateRange(
						    min_date, target_date, bind_data.frequency.interval);
					}
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
			idx_t other_col_offset = 0;

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
						output.SetValue(col_idx++, output_count, Value()); // NULL for forward-filled rows
					}
				} else {
					if (date_exists && original_index < series.other_columns.size() &&
					    other_col_offset < series.other_columns[original_index].size()) {
						output.SetValue(col_idx++, output_count,
						                series.other_columns[original_index][other_col_offset++]);
					} else {
						output.SetValue(col_idx++, output_count, Value()); // NULL for forward-filled rows
						other_col_offset++;
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
				date_value = Value::DATE(ts_fill_forward_internal::TimePointToDate(time_point));
			} else {
				date_value = Value::TIMESTAMP(ts_fill_forward_internal::TimePointToTimestamp(time_point));
			}

			// Emit row (same logic as integer mode)
			idx_t col_idx = 0;
			idx_t other_col_offset = 0;

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
						output.SetValue(col_idx++, output_count, Value()); // NULL for forward-filled rows
					}
				} else {
					if (date_exists && original_index < series.other_columns.size() &&
					    other_col_offset < series.other_columns[original_index].size()) {
						output.SetValue(col_idx++, output_count,
						                series.other_columns[original_index][other_col_offset++]);
					} else {
						output.SetValue(col_idx++, output_count, Value()); // NULL for forward-filled rows
						other_col_offset++;
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
unique_ptr<NodeStatistics> TSFillForwardCardinality(ClientContext &context, const FunctionData *bind_data) {
	// For table-in-out functions, cardinality is typically unknown until all input is processed
	return nullptr;
}

} // namespace duckdb
