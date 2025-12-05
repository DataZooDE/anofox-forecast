#include "mstl_decomposition_function.hpp"
#include "anofox-time/seasonality/mstl.hpp"
#include "anofox-time/core/time_series.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/keyword_helper.hpp"

namespace duckdb {

namespace mstl_internal {

// Helper functions for date conversion (copied/adapted from ts_fill_gaps_function.cpp)
std::chrono::system_clock::time_point DateToTimePoint(const date_t &date) {
	int64_t days = date.days;
	int64_t micros = days * 24LL * 60LL * 60LL * 1000000LL;
	std::chrono::microseconds duration(micros);
	return std::chrono::system_clock::time_point(duration);
}

std::chrono::system_clock::time_point TimestampToTimePoint(const timestamp_t &ts) {
	std::chrono::microseconds duration(ts.value);
	return std::chrono::system_clock::time_point(duration);
}

std::chrono::system_clock::time_point ConvertToTimePoint(const Value &date_value, const LogicalType &date_type) {
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

struct DataPoint {
	std::chrono::system_clock::time_point time;
	double value;
	std::vector<Value> other_cols;
	Value original_date_val; // To preserve exact original date value type
};

} // namespace mstl_internal

struct TSMstlDecompositionBindData : public TableFunctionData {
	std::string group_col;
	std::string date_col;
	std::string value_col;
	std::vector<int32_t> seasonal_periods;

	idx_t group_col_idx = DConstants::INVALID_INDEX;
	idx_t date_col_idx = DConstants::INVALID_INDEX;
	idx_t value_col_idx = DConstants::INVALID_INDEX;
	LogicalType date_col_type;

	vector<LogicalType> return_types;
	vector<string> return_names;
};

struct TSMstlDecompositionGlobalState : public GlobalTableFunctionState {
	const TSMstlDecompositionBindData *bind_data = nullptr;
};

struct GroupData {
	std::vector<mstl_internal::DataPoint> points;
	Value group_value;
};

struct TSMstlDecompositionLocalState : public LocalTableFunctionState {
	std::unordered_map<std::string, GroupData> groups;
	std::vector<std::string> group_order; // To maintain processing order
	bool input_done = false;

	// Iteration state for Final
	idx_t current_group_idx = 0;
	idx_t current_row_idx = 0;

	// Buffer for processed results of current group
	struct ProcessedGroup {
		std::vector<double> trend;
		std::vector<std::vector<double>> seasonal;
		std::vector<double> residual;
	} current_processed;
};

// Extract seasonal periods from params value
std::vector<int32_t> TSMstlExtractPeriods(const Value &params_val) {
	std::vector<int32_t> seasonal_periods;

	if (params_val.IsNull()) {
		throw InvalidInputException("params cannot be NULL");
	}

	// Handle MAP
	if (params_val.type().id() == LogicalTypeId::MAP) {
		// Iterate over the list of entries if it's a MAP.
		// Note: MAPs in DuckDB are physically LIST(STRUCT(key, value)).
		const auto &entries = ListValue::GetChildren(params_val);
		for (const auto &entry : entries) {
			// entry is a STRUCT(key, value)
			auto &entry_children = StructValue::GetChildren(entry);
			if (entry_children.size() == 2) {
				string key = entry_children[0].ToString();
				if (key == "seasonal_periods") {
					const auto &val = entry_children[1];
					if (val.type().id() == LogicalTypeId::LIST) {
						const auto &list_children = ListValue::GetChildren(val);
						for (const auto &child : list_children) {
							seasonal_periods.push_back(child.GetValue<int32_t>());
						}
					}
					break;
				}
			}
		}
	}
	// Handle STRUCT (also common for params)
	else if (params_val.type().id() == LogicalTypeId::STRUCT) {
		auto &child_types = StructType::GetChildTypes(params_val.type());
		auto &children = StructValue::GetChildren(params_val);

		for (size_t i = 0; i < child_types.size(); i++) {
			if (child_types[i].first == "seasonal_periods") {
				const auto &list_val = children[i];
				if (list_val.type().id() == LogicalTypeId::LIST) {
					const auto &list_children = ListValue::GetChildren(list_val);
					for (const auto &child : list_children) {
						seasonal_periods.push_back(child.GetValue<int32_t>());
					}
				}
				break;
			}
		}
	}

	if (seasonal_periods.empty()) {
		throw InvalidInputException("seasonal_periods parameter is required in params (MAP or STRUCT)");
	}

	return seasonal_periods;
}

unique_ptr<FunctionData> TSMstlDecompositionBind(ClientContext &context, TableFunctionBindInput &input,
                                                 vector<LogicalType> &return_types, vector<string> &names) {
	if (input.inputs.size() < 5) {
		throw InvalidInputException("anofox_fcst_mstl_decomposition_operator requires 5 arguments: table, group_col, "
		                            "date_col, value_col, params");
	}

	if (input.input_table_types.empty() || input.input_table_names.empty()) {
		throw InvalidInputException("anofox_fcst_mstl_decomposition_operator requires TABLE input");
	}

	// Extract column names
	// Index 0 is the TABLE parameter (handled via input_table_types)
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();
	string value_col = input.inputs[3].ToString();

	// Extract params (5th argument -> index 4)
	Value params_val = input.inputs[4];

	std::vector<int32_t> seasonal_periods = TSMstlExtractPeriods(params_val);

	if (seasonal_periods.empty()) {
		throw InvalidInputException("seasonal_periods cannot be empty");
	}

	// Find column indices
	idx_t group_col_idx = DConstants::INVALID_INDEX;
	idx_t date_col_idx = DConstants::INVALID_INDEX;
	idx_t value_col_idx = DConstants::INVALID_INDEX;

	for (idx_t i = 0; i < input.input_table_names.size(); i++) {
		if (input.input_table_names[i] == group_col)
			group_col_idx = i;
		if (input.input_table_names[i] == date_col)
			date_col_idx = i;
		if (input.input_table_names[i] == value_col)
			value_col_idx = i;
	}

	if (group_col_idx == DConstants::INVALID_INDEX)
		throw InvalidInputException("Column '" + group_col + "' not found");
	if (date_col_idx == DConstants::INVALID_INDEX)
		throw InvalidInputException("Column '" + date_col + "' not found");
	if (value_col_idx == DConstants::INVALID_INDEX)
		throw InvalidInputException("Column '" + value_col + "' not found");

	auto bind_data = make_uniq<TSMstlDecompositionBindData>();
	bind_data->group_col = group_col;
	bind_data->date_col = date_col;
	bind_data->value_col = value_col;
	bind_data->seasonal_periods = seasonal_periods;
	bind_data->group_col_idx = group_col_idx;
	bind_data->date_col_idx = date_col_idx;
	bind_data->value_col_idx = value_col_idx;
	bind_data->date_col_type = input.input_table_types[date_col_idx];

	// Setup return types: Original columns + trend + seasonal_P... + residual
	return_types = input.input_table_types;
	names = input.input_table_names;

	return_types.push_back(LogicalType::DOUBLE);
	names.push_back("trend");

	for (int32_t p : seasonal_periods) {
		return_types.push_back(LogicalType::DOUBLE);
		names.push_back("seasonal_" + std::to_string(p));
	}

	return_types.push_back(LogicalType::DOUBLE);
	names.push_back("residual");

	bind_data->return_types = return_types;
	bind_data->return_names = names;

	return std::move(bind_data);
}

unique_ptr<GlobalTableFunctionState> TSMstlDecompositionInitGlobal(ClientContext &context,
                                                                   TableFunctionInitInput &input) {
	auto global_state = make_uniq<TSMstlDecompositionGlobalState>();
	global_state->bind_data = &input.bind_data->Cast<TSMstlDecompositionBindData>();
	return std::move(global_state);
}

unique_ptr<LocalTableFunctionState> TSMstlDecompositionInitLocal(ExecutionContext &context,
                                                                 TableFunctionInitInput &input,
                                                                 GlobalTableFunctionState *global_state) {
	return make_uniq<TSMstlDecompositionLocalState>();
}

OperatorResultType TSMstlDecompositionOperatorInOut(ExecutionContext &context, TableFunctionInput &data_p,
                                                    DataChunk &input, DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<TSMstlDecompositionBindData>();
	auto &lstate = data_p.local_state->Cast<TSMstlDecompositionLocalState>();

	if (input.size() == 0) {
		output.SetCardinality(0);
		return OperatorResultType::NEED_MORE_INPUT;
	}

	// Accumulate data
	for (idx_t i = 0; i < input.size(); i++) {
		auto group_val = input.data[bind_data.group_col_idx].GetValue(i);
		auto date_val = input.data[bind_data.date_col_idx].GetValue(i);
		auto value_val = input.data[bind_data.value_col_idx].GetValue(i);

		if (date_val.IsNull() || value_val.IsNull()) {
			continue;
		}

		string group_key = group_val.ToString();
		if (lstate.groups.find(group_key) == lstate.groups.end()) {
			lstate.groups[group_key] = GroupData();
			lstate.groups[group_key].group_value = group_val;
			lstate.group_order.push_back(group_key);
		}

		mstl_internal::DataPoint point;
		point.time = mstl_internal::ConvertToTimePoint(date_val, bind_data.date_col_type);
		point.value = value_val.GetValue<double>();
		point.original_date_val = date_val;

		// Store other columns
		for (idx_t col = 0; col < input.ColumnCount(); col++) {
			if (col != bind_data.group_col_idx && col != bind_data.date_col_idx && col != bind_data.value_col_idx) {
				point.other_cols.push_back(input.data[col].GetValue(i));
			}
		}

		lstate.groups[group_key].points.push_back(std::move(point));
	}

	output.SetCardinality(0);
	return OperatorResultType::NEED_MORE_INPUT;
}

OperatorFinalizeResultType TSMstlDecompositionOperatorFinal(ExecutionContext &context, TableFunctionInput &data_p,
                                                            DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<TSMstlDecompositionBindData>();
	auto &lstate = data_p.local_state->Cast<TSMstlDecompositionLocalState>();

	if (!lstate.input_done) {
		lstate.input_done = true;
		lstate.current_group_idx = 0;
		lstate.current_row_idx = 0;
	}

	idx_t out_count = 0;
	output.SetCardinality(0);

	if (output.ColumnCount() == 0) {
		output.InitializeEmpty(bind_data.return_types);
	}

	// Initialize output columns - set vector type to FLAT_VECTOR for all columns
	// This is important for proper batch handling in parallel execution
	// Must be done on every call to Final, not just when ColumnCount() == 0
	for (idx_t col_idx = 0; col_idx < output.ColumnCount(); col_idx++) {
		output.data[col_idx].SetVectorType(VectorType::FLAT_VECTOR);
	}

	// Early return if no groups to process
	if (lstate.group_order.empty()) {
		output.SetCardinality(0);
		return OperatorFinalizeResultType::FINISHED;
	}

	while (out_count < STANDARD_VECTOR_SIZE && lstate.current_group_idx < lstate.group_order.size()) {
		string group_key = lstate.group_order[lstate.current_group_idx];
		auto &group = lstate.groups[group_key];

		// Process group if not yet processed
		if (lstate.current_row_idx == 0) {
			// Sort points by time
			std::sort(
			    group.points.begin(), group.points.end(),
			    [](const mstl_internal::DataPoint &a, const mstl_internal::DataPoint &b) { return a.time < b.time; });

			// Create TimeSeries
			std::vector<anofoxtime::core::TimeSeries::TimePoint> timestamps;
			std::vector<double> values;
			timestamps.reserve(group.points.size());
			values.reserve(group.points.size());

			for (const auto &p : group.points) {
				timestamps.push_back(p.time);
				values.push_back(p.value);
			}

			// Run MSTL
			try {
				anofoxtime::core::TimeSeries ts(timestamps, values);

				// Convert int32_t periods to size_t
				std::vector<std::size_t> periods;
				for (auto p : bind_data.seasonal_periods)
					periods.push_back(static_cast<std::size_t>(p));

				auto decomposer = anofoxtime::seasonality::MSTLDecomposition::builder()
				                      .withPeriods(periods)
				                      .withIterations(2) // Default
				                      .build();

				decomposer.fit(ts);
				const auto &comps = decomposer.components();

				lstate.current_processed.trend = comps.trend;
				lstate.current_processed.seasonal = comps.seasonal;
				lstate.current_processed.residual = comps.remainder;

			} catch (const std::exception &e) {
				throw InvalidInputException("MSTL decomposition failed for group " + group_key + ": " + e.what());
			}
		}

		// Emit rows
		idx_t count_in_group = group.points.size();
		while (out_count < STANDARD_VECTOR_SIZE && lstate.current_row_idx < count_in_group) {
			const auto &point = group.points[lstate.current_row_idx];

			// 1. Original columns
			idx_t col_idx = 0;
			idx_t other_col_idx = 0;
			for (idx_t i = 0; i < bind_data.return_types.size() - (2 + bind_data.seasonal_periods.size()); i++) {
				if (i == bind_data.group_col_idx) {
					output.SetValue(col_idx++, out_count, group.group_value);
				} else if (i == bind_data.date_col_idx) {
					output.SetValue(col_idx++, out_count, point.original_date_val);
				} else if (i == bind_data.value_col_idx) {
					output.SetValue(col_idx++, out_count, Value::DOUBLE(point.value));
				} else {
					output.SetValue(col_idx++, out_count, point.other_cols[other_col_idx++]);
				}
			}

			// 2. MSTL components
			// trend
			output.SetValue(col_idx++, out_count,
			                Value::DOUBLE(lstate.current_processed.trend[lstate.current_row_idx]));

			// seasonal components
			for (size_t s = 0; s < bind_data.seasonal_periods.size(); s++) {
				output.SetValue(col_idx++, out_count,
				                Value::DOUBLE(lstate.current_processed.seasonal[s][lstate.current_row_idx]));
			}

			// residual
			output.SetValue(col_idx++, out_count,
			                Value::DOUBLE(lstate.current_processed.residual[lstate.current_row_idx]));

			lstate.current_row_idx++;
			out_count++;
		}

		if (lstate.current_row_idx >= count_in_group) {
			lstate.current_group_idx++;
			lstate.current_row_idx = 0;
		}
	}

	output.SetCardinality(out_count);

	// Safety check: if we have no output but more groups, something went wrong
	if (out_count == 0 && lstate.current_group_idx < lstate.group_order.size()) {
		// This shouldn't happen, but if it does, return FINISHED to avoid issues
		return OperatorFinalizeResultType::FINISHED;
	}

	if (lstate.current_group_idx >= lstate.group_order.size()) {
		return OperatorFinalizeResultType::FINISHED;
	}

	// Only return HAVE_MORE_OUTPUT if we actually have output
	if (out_count > 0) {
		return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
	}

	return OperatorFinalizeResultType::FINISHED;
}

// Cardinality estimation
unique_ptr<NodeStatistics> TSMstlDecompositionCardinality(ClientContext &context, const FunctionData *bind_data) {
	// For table-in-out functions, cardinality is typically unknown until all input is processed
	// Return nullptr to let DuckDB estimate
	return nullptr;
}

// Bind Replace for SQL Wrapper
unique_ptr<TableRef> TSMstlDecompositionBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	// Helper to get argument (named or positional)
	auto get_arg = [&](const string &name, idx_t pos) -> Value {
		auto it = input.named_parameters.find(name);
		if (it != input.named_parameters.end()) {
			return it->second;
		}
		if (pos < input.inputs.size()) {
			return input.inputs[pos];
		}
		return Value();
	};

	Value table_name_val = get_arg("table_name", 0);
	Value group_col_val = get_arg("group_col", 1);
	Value date_col_val = get_arg("date_col", 2);
	Value value_col_val = get_arg("value_col", 3);
	Value params_val = get_arg("params", 4);

	if (table_name_val.IsNull())
		throw InvalidInputException("table_name argument is required");
	if (group_col_val.IsNull())
		throw InvalidInputException("group_col argument is required");
	if (date_col_val.IsNull())
		throw InvalidInputException("date_col argument is required");
	if (value_col_val.IsNull())
		throw InvalidInputException("value_col argument is required");
	// params can be null/empty if not provided? The operator might need it.
	// Actually operator throws if params is null.

	string table_name = table_name_val.ToString();
	string group_col = group_col_val.ToString();
	string date_col = date_col_val.ToString();
	string value_col = value_col_val.ToString();
	string params_sql = params_val.IsNull() ? "NULL" : params_val.ToSQLString();

	string escaped_table = KeywordHelper::WriteQuoted(table_name);

	// We pass column names as string literals to the operator
	std::ostringstream sql;
	sql << "SELECT * FROM anofox_fcst_mstl_decomposition_operator("
	    << "(SELECT * FROM " << escaped_table << "), "
	    << "'" << group_col << "', "
	    << "'" << date_col << "', "
	    << "'" << value_col << "', " << params_sql << ")";

	// Parse subquery
	Parser parser(context.GetParserOptions());
	parser.ParseQuery(sql.str());
	if (parser.statements.size() != 1 || parser.statements[0]->type != StatementType::SELECT_STATEMENT) {
		throw ParserException("Failed to generate SQL for TS_MSTL_DECOMPOSITION");
	}
	auto select_stmt = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	return duckdb::make_uniq<SubqueryRef>(std::move(select_stmt));
}

void RegisterMstlDecompositionFunctions(ExtensionLoader &loader) {
	// Register Operator
	// 5th argument is ANY (params)
	TableFunction operator_func(
	    "anofox_fcst_mstl_decomposition_operator",
	    {LogicalType::TABLE, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::ANY},
	    nullptr, TSMstlDecompositionBind, TSMstlDecompositionInitGlobal, TSMstlDecompositionInitLocal);
	operator_func.in_out_function = TSMstlDecompositionOperatorInOut;
	operator_func.in_out_function_final = TSMstlDecompositionOperatorFinal;
	operator_func.cardinality = TSMstlDecompositionCardinality;

	TableFunctionSet set("anofox_fcst_mstl_decomposition_operator");
	set.AddFunction(operator_func);
	CreateTableFunctionInfo info(std::move(set));
	loader.RegisterFunction(std::move(info));

	// Register Macro/BindReplace
	// Flexible arguments: allow positional or named
	TableFunction macro_func("anofox_fcst_ts_mstl_decomposition", {}, // No fixed positional args
	                         nullptr, nullptr);
	macro_func.bind_replace = TSMstlDecompositionBindReplace;
	macro_func.varargs = LogicalType::ANY; // Accept any positional arguments

	macro_func.named_parameters["table_name"] = LogicalType::VARCHAR;
	macro_func.named_parameters["group_col"] = LogicalType::VARCHAR;
	macro_func.named_parameters["date_col"] = LogicalType::VARCHAR;
	macro_func.named_parameters["value_col"] = LogicalType::VARCHAR;
	macro_func.named_parameters["params"] = LogicalType::ANY;

	loader.RegisterFunction(macro_func);

	// Register alias without prefix
	TableFunction alias_func = macro_func;
	alias_func.name = "ts_mstl_decomposition";
	TableFunctionSet alias_set("ts_mstl_decomposition");
	alias_set.AddFunction(alias_func);
	CreateTableFunctionInfo alias_info(std::move(alias_set));
	alias_info.alias_of = "anofox_fcst_ts_mstl_decomposition";
	alias_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(alias_info));
}

} // namespace duckdb
