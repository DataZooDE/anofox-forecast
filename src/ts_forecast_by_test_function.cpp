#include "include/ts_forecast_by_test_function.hpp"
#include "model_factory.hpp"
#include "time_series_builder.hpp"
#include "anofox_time_wrapper.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression/function_expression.hpp"
#include "duckdb/parser/tableref/subqueryref.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/parser/keyword_helper.hpp"

#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include "anofox-time/models/iforecaster.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace duckdb {

namespace forecast_by_test_internal {

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
	} else if (date_type.id() == LogicalTypeId::INTEGER || date_type.id() == LogicalTypeId::BIGINT) {
		// Treat integer as microseconds offset
		int64_t micros = date_value.GetValue<int64_t>() * 24LL * 60LL * 60LL * 1000000LL;
		return std::chrono::system_clock::time_point(std::chrono::microseconds(micros));
	} else {
		throw InvalidInputException("Unsupported date column type for time_point conversion");
	}
}

// Helper to extract parameter from STRUCT
template <typename T>
T GetStructParam(const Value &params, const std::string &key, T default_value) {
	if (params.IsNull() || params.type().id() != LogicalTypeId::STRUCT) {
		return default_value;
	}
	auto &struct_children = StructValue::GetChildren(params);
	for (size_t i = 0; i < struct_children.size(); i++) {
		auto &child_key = StructType::GetChildName(params.type(), i);
		if (child_key == key) {
			try {
				return struct_children[i].GetValue<T>();
			} catch (...) {
				return default_value;
			}
		}
	}
	return default_value;
}

} // namespace forecast_by_test_internal

unique_ptr<FunctionData> TSForecastByTestOperatorBind(ClientContext &context, TableFunctionBindInput &input,
                                                      vector<LogicalType> &return_types, vector<string> &names) {
	// Expected arguments: TABLE, group_col, date_col, target_col, method, horizon, params
	if (input.inputs.size() < 7) {
		throw InvalidInputException(
		    "anofox_fcst_ts_forecast_by_test_operator requires 7 arguments: table, group_col, "
		    "date_col, target_col, method, horizon, params");
	}

	if (input.input_table_types.empty() || input.input_table_names.empty()) {
		throw InvalidInputException("anofox_fcst_ts_forecast_by_test_operator requires TABLE input");
	}

	// Extract column names (index 0 is TABLE, handled via input_table_types)
	string group_col = input.inputs[1].ToString();
	string date_col = input.inputs[2].ToString();
	string target_col = input.inputs[3].ToString();
	// Method is ignored for now - fixed to AutoARIMA
	// string method = input.inputs[4].ToString();
	int32_t horizon = input.inputs[5].GetValue<int32_t>();
	Value params = input.inputs[6];

	if (horizon <= 0) {
		throw InvalidInputException("horizon must be positive");
	}

	// Find column indices
	idx_t group_col_idx = DConstants::INVALID_INDEX;
	idx_t date_col_idx = DConstants::INVALID_INDEX;
	idx_t target_col_idx = DConstants::INVALID_INDEX;

	for (idx_t i = 0; i < input.input_table_names.size(); i++) {
		if (input.input_table_names[i] == group_col) {
			group_col_idx = i;
		}
		if (input.input_table_names[i] == date_col) {
			date_col_idx = i;
		}
		if (input.input_table_names[i] == target_col) {
			target_col_idx = i;
		}
	}

	if (group_col_idx == DConstants::INVALID_INDEX) {
		throw InvalidInputException("Column '" + group_col + "' not found");
	}
	if (date_col_idx == DConstants::INVALID_INDEX) {
		throw InvalidInputException("Column '" + date_col + "' not found");
	}
	if (target_col_idx == DConstants::INVALID_INDEX) {
		throw InvalidInputException("Column '" + target_col + "' not found");
	}

	// Validate date column type
	auto &date_type = input.input_table_types[date_col_idx];
	if (date_type.id() != LogicalTypeId::DATE && date_type.id() != LogicalTypeId::TIMESTAMP &&
	    date_type.id() != LogicalTypeId::INTEGER && date_type.id() != LogicalTypeId::BIGINT) {
		throw InvalidInputException("Date column must be DATE, TIMESTAMP, INTEGER, or BIGINT");
	}

	// Extract parameters from params struct
	using namespace forecast_by_test_internal;
	double confidence_level = GetStructParam<double>(params, "confidence_level", 0.90);
	int32_t seasonal_period = GetStructParam<int32_t>(params, "seasonal_period", 0);
	bool insample_forecast = GetStructParam<bool>(params, "insample_forecast", false);

	if (confidence_level <= 0.0 || confidence_level >= 1.0) {
		throw InvalidInputException("confidence_level must be between 0 and 1");
	}

	// Build column names based on confidence level
	int confidence_pct = static_cast<int>(confidence_level * 100);
	std::string lower_col_name = "lower_" + std::to_string(confidence_pct);
	std::string upper_col_name = "upper_" + std::to_string(confidence_pct);

	// Build return schema
	// For forecast-only mode: group_col, forecast_step, date, point_forecast, lower_X, upper_X, model_name
	return_types.push_back(input.input_table_types[group_col_idx]); // group column
	names.push_back(group_col);

	return_types.push_back(LogicalType::INTEGER); // forecast_step
	names.push_back("forecast_step");

	return_types.push_back(date_type); // date
	names.push_back("date");

	return_types.push_back(LogicalType::DOUBLE); // point_forecast
	names.push_back("point_forecast");

	return_types.push_back(LogicalType::DOUBLE); // lower bound
	names.push_back(lower_col_name);

	return_types.push_back(LogicalType::DOUBLE); // upper bound
	names.push_back(upper_col_name);

	return_types.push_back(LogicalType::VARCHAR); // model_name
	names.push_back("model_name");

	// Create bind data
	auto bind_data = make_uniq<TSForecastByTestBindData>();
	bind_data->group_col = group_col;
	bind_data->date_col = date_col;
	bind_data->target_col = target_col;
	bind_data->group_col_idx = group_col_idx;
	bind_data->date_col_idx = date_col_idx;
	bind_data->target_col_idx = target_col_idx;
	bind_data->group_col_type = input.input_table_types[group_col_idx];
	bind_data->date_col_type = date_type;
	bind_data->horizon = horizon;
	bind_data->seasonal_period = seasonal_period;
	bind_data->confidence_level = confidence_level;
	bind_data->insample_forecast = insample_forecast;
	bind_data->lower_col_name = lower_col_name;
	bind_data->upper_col_name = upper_col_name;
	bind_data->return_types = return_types;
	bind_data->return_names = names;

	return std::move(bind_data);
}

unique_ptr<GlobalTableFunctionState> TSForecastByTestInitGlobal(ClientContext &context,
                                                                 TableFunctionInitInput &input) {
	auto global_state = make_uniq<TSForecastByTestGlobalState>();
	global_state->bind_data = &input.bind_data->Cast<TSForecastByTestBindData>();
	return std::move(global_state);
}

unique_ptr<LocalTableFunctionState> TSForecastByTestInitLocal(ExecutionContext &context, TableFunctionInitInput &input,
                                                               GlobalTableFunctionState *global_state) {
	return make_uniq<TSForecastByTestLocalState>();
}

OperatorResultType TSForecastByTestOperatorInOut(ExecutionContext &context, TableFunctionInput &data_p,
                                                  DataChunk &input, DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<TSForecastByTestBindData>();
	auto &lstate = data_p.local_state->Cast<TSForecastByTestLocalState>();

	if (input.size() == 0) {
		output.SetCardinality(0);
		return OperatorResultType::NEED_MORE_INPUT;
	}

	// Accumulate data
	for (idx_t i = 0; i < input.size(); i++) {
		auto group_val = input.data[bind_data.group_col_idx].GetValue(i);
		auto date_val = input.data[bind_data.date_col_idx].GetValue(i);
		auto value_val = input.data[bind_data.target_col_idx].GetValue(i);

		if (date_val.IsNull() || value_val.IsNull()) {
			continue;
		}

		std::string group_key = group_val.ToString();
		if (lstate.groups.find(group_key) == lstate.groups.end()) {
			lstate.groups[group_key] = ForecastGroupData();
			lstate.groups[group_key].group_value = group_val;
			lstate.group_order.push_back(group_key);
		}

		ForecastDataPoint point;
		point.timestamp = forecast_by_test_internal::ConvertToTimePoint(date_val, bind_data.date_col_type);
		point.value = value_val.GetValue<double>();
		point.original_date_val = date_val;

		lstate.groups[group_key].points.push_back(std::move(point));
	}

	output.SetCardinality(0);
	return OperatorResultType::NEED_MORE_INPUT;
}

OperatorFinalizeResultType TSForecastByTestOperatorFinal(ExecutionContext &context, TableFunctionInput &data_p,
                                                          DataChunk &output) {
	auto &bind_data = data_p.bind_data->Cast<TSForecastByTestBindData>();
	auto &lstate = data_p.local_state->Cast<TSForecastByTestLocalState>();

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

	// Set vector type to FLAT_VECTOR for all columns
	for (idx_t col_idx = 0; col_idx < output.ColumnCount(); col_idx++) {
		output.data[col_idx].SetVectorType(VectorType::FLAT_VECTOR);
	}

	// Early return if no groups
	if (lstate.group_order.empty()) {
		output.SetCardinality(0);
		return OperatorFinalizeResultType::FINISHED;
	}

	while (out_count < STANDARD_VECTOR_SIZE && lstate.current_group_idx < lstate.group_order.size()) {
		std::string group_key = lstate.group_order[lstate.current_group_idx];
		auto &group = lstate.groups[group_key];

		// Process group if starting fresh
		if (lstate.current_row_idx == 0) {
			// Sort points by time
			std::sort(group.points.begin(), group.points.end(),
			          [](const ForecastDataPoint &a, const ForecastDataPoint &b) { return a.timestamp < b.timestamp; });

			// Remove duplicate timestamps (keep last)
			auto last =
			    std::unique(group.points.begin(), group.points.end(),
			                [](const ForecastDataPoint &a, const ForecastDataPoint &b) { return a.timestamp == b.timestamp; });
			group.points.erase(last, group.points.end());

			// Convert to TimeSeries
			std::vector<anofoxtime::core::TimeSeries::TimePoint> timestamps;
			std::vector<double> values;
			timestamps.reserve(group.points.size());
			values.reserve(group.points.size());

			for (const auto &p : group.points) {
				timestamps.push_back(p.timestamp);
				values.push_back(p.value);
			}

			try {
				// Build TimeSeries
				auto ts_ptr = TimeSeriesBuilder::BuildTimeSeries(timestamps, values);

				// Create model params
				Value model_params = Value::STRUCT({{"seasonal_period", Value::INTEGER(bind_data.seasonal_period)}});

				// Create and fit AutoARIMA model
				auto model_ptr = ModelFactory::Create("AutoARIMA", model_params);
				AnofoxTimeWrapper::FitModel(model_ptr.get(), *ts_ptr);

				// Predict with confidence
				auto forecast_ptr =
				    AnofoxTimeWrapper::PredictWithConfidence(model_ptr.get(), bind_data.horizon, bind_data.confidence_level);

				auto &primary_forecast = AnofoxTimeWrapper::GetPrimaryForecast(*forecast_ptr);
				bool has_intervals =
				    AnofoxTimeWrapper::HasLowerBound(*forecast_ptr) && AnofoxTimeWrapper::HasUpperBound(*forecast_ptr);

				// Calculate forecast timestamps
				int64_t interval_micros = 0;
				int64_t last_timestamp_micros = 0;

				if (timestamps.size() >= 2) {
					auto total_time =
					    std::chrono::duration_cast<std::chrono::microseconds>(timestamps.back() - timestamps.front()).count();
					interval_micros = total_time / (static_cast<int64_t>(timestamps.size()) - 1);
				}
				last_timestamp_micros =
				    std::chrono::duration_cast<std::chrono::microseconds>(timestamps.back().time_since_epoch()).count();

				// Store forecast results
				lstate.current_forecast.forecast_steps.clear();
				lstate.current_forecast.forecast_timestamps.clear();
				lstate.current_forecast.point_forecasts.clear();
				lstate.current_forecast.lower_bounds.clear();
				lstate.current_forecast.upper_bounds.clear();
				lstate.current_forecast.model_name = "AutoARIMA";

				for (int32_t h = 0; h < bind_data.horizon; h++) {
					lstate.current_forecast.forecast_steps.push_back(h + 1);
					lstate.current_forecast.point_forecasts.push_back(primary_forecast[h]);

					if (has_intervals) {
						auto &lower_bound = AnofoxTimeWrapper::GetLowerBound(*forecast_ptr);
						auto &upper_bound = AnofoxTimeWrapper::GetUpperBound(*forecast_ptr);
						lstate.current_forecast.lower_bounds.push_back(lower_bound[h]);
						lstate.current_forecast.upper_bounds.push_back(upper_bound[h]);
					} else {
						// Fallback: use +/- 10% as placeholder
						lstate.current_forecast.lower_bounds.push_back(primary_forecast[h] * 0.9);
						lstate.current_forecast.upper_bounds.push_back(primary_forecast[h] * 1.1);
					}

					// Generate forecast timestamp
					int64_t forecast_ts_micros = last_timestamp_micros + interval_micros * (h + 1);
					if (bind_data.date_col_type.id() == LogicalTypeId::DATE) {
						// Convert micros to days
						int32_t days = static_cast<int32_t>(forecast_ts_micros / (24LL * 60LL * 60LL * 1000000LL));
						lstate.current_forecast.forecast_timestamps.push_back(Value::DATE(date_t(days)));
					} else if (bind_data.date_col_type.id() == LogicalTypeId::TIMESTAMP) {
						lstate.current_forecast.forecast_timestamps.push_back(Value::TIMESTAMP(timestamp_t(forecast_ts_micros)));
					} else {
						// INTEGER/BIGINT - convert back to day index
						int64_t day_index = forecast_ts_micros / (24LL * 60LL * 60LL * 1000000LL);
						lstate.current_forecast.forecast_timestamps.push_back(Value::BIGINT(day_index));
					}
				}

			} catch (const std::exception &e) {
				throw InvalidInputException("Forecasting failed for group " + group_key + ": " + e.what());
			}
		}

		// Emit forecast rows for current group
		idx_t forecast_size = static_cast<idx_t>(lstate.current_forecast.forecast_steps.size());
		while (out_count < STANDARD_VECTOR_SIZE && lstate.current_row_idx < forecast_size) {
			idx_t col_idx = 0;

			// group_col
			output.SetValue(col_idx++, out_count, group.group_value);

			// forecast_step
			output.SetValue(col_idx++, out_count, Value::INTEGER(lstate.current_forecast.forecast_steps[lstate.current_row_idx]));

			// date
			output.SetValue(col_idx++, out_count, lstate.current_forecast.forecast_timestamps[lstate.current_row_idx]);

			// point_forecast
			output.SetValue(col_idx++, out_count, Value::DOUBLE(lstate.current_forecast.point_forecasts[lstate.current_row_idx]));

			// lower bound
			output.SetValue(col_idx++, out_count, Value::DOUBLE(lstate.current_forecast.lower_bounds[lstate.current_row_idx]));

			// upper bound
			output.SetValue(col_idx++, out_count, Value::DOUBLE(lstate.current_forecast.upper_bounds[lstate.current_row_idx]));

			// model_name
			output.SetValue(col_idx++, out_count, Value(lstate.current_forecast.model_name));

			lstate.current_row_idx++;
			out_count++;
		}

		// Move to next group if done with current
		if (lstate.current_row_idx >= forecast_size) {
			lstate.current_group_idx++;
			lstate.current_row_idx = 0;
		}
	}

	output.SetCardinality(out_count);

	if (lstate.current_group_idx >= lstate.group_order.size()) {
		return OperatorFinalizeResultType::FINISHED;
	}

	if (out_count > 0) {
		return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
	}

	return OperatorFinalizeResultType::FINISHED;
}

unique_ptr<NodeStatistics> TSForecastByTestCardinality(ClientContext &context, const FunctionData *bind_data) {
	// Cardinality is unknown until all input is processed
	return nullptr;
}

unique_ptr<TableRef> TSForecastByTestBindReplace(ClientContext &context, TableFunctionBindInput &input) {
	// Helper to get argument (named or positional)
	auto get_arg = [&](const std::string &name, idx_t pos) -> Value {
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
	Value target_col_val = get_arg("target_col", 3);
	Value method_val = get_arg("method", 4);
	Value horizon_val = get_arg("horizon", 5);
	Value params_val = get_arg("params", 6);

	if (table_name_val.IsNull()) {
		throw InvalidInputException("table_name argument is required");
	}
	if (group_col_val.IsNull()) {
		throw InvalidInputException("group_col argument is required");
	}
	if (date_col_val.IsNull()) {
		throw InvalidInputException("date_col argument is required");
	}
	if (target_col_val.IsNull()) {
		throw InvalidInputException("target_col argument is required");
	}
	if (horizon_val.IsNull()) {
		throw InvalidInputException("horizon argument is required");
	}

	std::string table_name = table_name_val.ToString();
	std::string group_col = group_col_val.ToString();
	std::string date_col = date_col_val.ToString();
	std::string target_col = target_col_val.ToString();
	std::string method = method_val.IsNull() ? "AutoARIMA" : method_val.ToString();
	std::string horizon_str = horizon_val.ToString();
	std::string params_sql = params_val.IsNull() ? "NULL" : params_val.ToSQLString();

	std::string escaped_table = KeywordHelper::WriteQuoted(table_name);

	// Build SQL query
	std::ostringstream sql;
	sql << "SELECT * FROM anofox_fcst_ts_forecast_by_test_operator("
	    << "(SELECT * FROM " << escaped_table << "), "
	    << "'" << group_col << "', "
	    << "'" << date_col << "', "
	    << "'" << target_col << "', "
	    << "'" << method << "', " << horizon_str << ", " << params_sql << ")";

	// Parse subquery
	Parser parser(context.GetParserOptions());
	parser.ParseQuery(sql.str());
	if (parser.statements.size() != 1 || parser.statements[0]->type != StatementType::SELECT_STATEMENT) {
		throw ParserException("Failed to generate SQL for ts_forecast_by_test");
	}
	auto select_stmt = unique_ptr_cast<SQLStatement, SelectStatement>(std::move(parser.statements[0]));
	return duckdb::make_uniq<SubqueryRef>(std::move(select_stmt));
}

void RegisterTSForecastByTestFunction(ExtensionLoader &loader) {
	// 1. Register internal operator (takes TABLE input)
	TableFunction operator_func(
	    "anofox_fcst_ts_forecast_by_test_operator",
	    {LogicalType::TABLE, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR,
	     LogicalType::INTEGER, LogicalType::ANY},
	    nullptr, TSForecastByTestOperatorBind, TSForecastByTestInitGlobal, TSForecastByTestInitLocal);
	operator_func.in_out_function = TSForecastByTestOperatorInOut;
	operator_func.in_out_function_final = TSForecastByTestOperatorFinal;
	operator_func.cardinality = TSForecastByTestCardinality;

	TableFunctionSet op_set("anofox_fcst_ts_forecast_by_test_operator");
	op_set.AddFunction(operator_func);
	CreateTableFunctionInfo op_info(std::move(op_set));
	loader.RegisterFunction(std::move(op_info));

	// 2. Register public API with bind_replace
	TableFunction macro_func("anofox_fcst_ts_forecast_by_test", {}, nullptr, nullptr);
	macro_func.bind_replace = TSForecastByTestBindReplace;
	macro_func.varargs = LogicalType::ANY;

	macro_func.named_parameters["table_name"] = LogicalType::VARCHAR;
	macro_func.named_parameters["group_col"] = LogicalType::VARCHAR;
	macro_func.named_parameters["date_col"] = LogicalType::VARCHAR;
	macro_func.named_parameters["target_col"] = LogicalType::VARCHAR;
	macro_func.named_parameters["method"] = LogicalType::VARCHAR;
	macro_func.named_parameters["horizon"] = LogicalType::INTEGER;
	macro_func.named_parameters["params"] = LogicalType::ANY;

	loader.RegisterFunction(macro_func);

	// 3. Register alias without prefix
	TableFunction alias_func = macro_func;
	alias_func.name = "ts_forecast_by_test";
	TableFunctionSet alias_set("ts_forecast_by_test");
	alias_set.AddFunction(alias_func);
	CreateTableFunctionInfo alias_info(std::move(alias_set));
	alias_info.alias_of = "anofox_fcst_ts_forecast_by_test";
	alias_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(alias_info));
}

} // namespace duckdb
