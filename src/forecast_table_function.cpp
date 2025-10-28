#include "forecast_table_function.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include <iostream>
#include <algorithm>

// Include full anofox-time types
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include "anofox-time/models/iforecaster.hpp"

namespace duckdb {

// Destructor
ForecastLocalState::~ForecastLocalState() {
	// std::cerr << "[DEBUG] ForecastLocalState destructor called" << std::endl;
	if (model) {
		delete model;
		model = nullptr;
	}
	if (forecast) {
		delete forecast;
		forecast = nullptr;
	}
}

unique_ptr<FunctionData> ForecastBind(ClientContext &context, TableFunctionBindInput &input,
                                      vector<LogicalType> &return_types, vector<string> &names) {
	// std::cerr << "[DEBUG] ForecastBind called with " << input.inputs.size() << " parameters" << std::endl;

	// For table-in-out function, parameters are:
	// - Input columns come from the piped table automatically
	// - Function arguments are the model configuration

	auto &inputs = input.inputs;
	if (inputs.size() < 2) {
		throw InvalidInputException("FORECAST function requires at least 2 parameters: model, horizon");
	}

	// Extract parameters (no table/column names - those come from input!)
	auto model_name = inputs[0].GetValue<string>();
	auto horizon = inputs[1].GetValue<int32_t>();

	// Optional model parameters
	Value model_params;
	if (inputs.size() > 2 && !inputs[2].IsNull()) {
		model_params = inputs[2];
	} else {
		model_params = Value::STRUCT({});
	}

	// std::cerr << "[DEBUG] FORECAST parameters - model: " << model_name
	//           << ", horizon: " << horizon << std::endl;

	// Validate horizon
	if (horizon <= 0) {
		throw InvalidInputException("Horizon must be positive, got: " + std::to_string(horizon));
	}

	// Validate model name
	auto supported_models = ModelFactory::GetSupportedModels();
	if (std::find(supported_models.begin(), supported_models.end(), model_name) == supported_models.end()) {
		string supported_list;
		for (size_t i = 0; i < supported_models.size(); i++) {
			if (i > 0)
				supported_list += ", ";
			supported_list += supported_models[i];
		}
		throw InvalidInputException("Unsupported model: '" + model_name + "'. Supported models: " + supported_list);
	}

	// Validate model parameters
	ModelFactory::ValidateModelParams(model_name, model_params);

	// Set output schema
	return_types = {
	    LogicalType::INTEGER, // forecast_step
	    LogicalType::DOUBLE,  // point_forecast
	    LogicalType::DOUBLE,  // lower_95
	    LogicalType::DOUBLE,  // upper_95
	    LogicalType::VARCHAR, // model_name
	    LogicalType::DOUBLE   // fit_time_ms
	};

	names = {"forecast_step", "point_forecast", "lower_95", "upper_95", "model_name", "fit_time_ms"};

	// Store configuration
	auto bind_data = make_uniq<ForecastBindData>();
	bind_data->model_name = model_name;
	bind_data->horizon = horizon;
	bind_data->model_params = model_params;

	// std::cerr << "[DEBUG] ForecastBind completed successfully" << std::endl;
	return std::move(bind_data);
}

unique_ptr<GlobalTableFunctionState> ForecastInitGlobal(ClientContext &context, TableFunctionInitInput &input) {
	// std::cerr << "[DEBUG] ForecastInitGlobal called" << std::endl;

	auto global_state = make_uniq<ForecastGlobalState>();
	global_state->bind_data = (ForecastBindData *)input.bind_data.get();

	// Store which columns to use from input
	// First column = timestamp, second column = value
	global_state->timestamp_col_idx = 0;
	global_state->value_col_idx = 1;

	// std::cerr << "[DEBUG] ForecastInitGlobal completed" << std::endl;
	return std::move(global_state);
}

unique_ptr<LocalTableFunctionState> ForecastInitLocal(ExecutionContext &context, TableFunctionInitInput &input,
                                                      GlobalTableFunctionState *global_state) {
	// std::cerr << "[DEBUG] ForecastInitLocal called" << std::endl;

	auto local_state = make_uniq<ForecastLocalState>();
	local_state->input_done = false;
	local_state->forecast_generated = false;
	local_state->output_offset = 0;
	local_state->model = nullptr;
	local_state->forecast = nullptr;

	// std::cerr << "[DEBUG] ForecastInitLocal completed" << std::endl;
	return std::move(local_state);
}

// Table-in-out function: accumulates input data
OperatorResultType ForecastInOutFunction(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                         DataChunk &output) {
	// std::cerr << "[DEBUG] ForecastInOutFunction called with " << input.size() << " input rows, "
	//           << input.ColumnCount() << " columns" << std::endl;

	auto &state = data_p.local_state->Cast<ForecastLocalState>();
	auto &gstate = data_p.global_state->Cast<ForecastGlobalState>();

	// Accumulate input data
	if (input.size() > 0) {
		// std::cerr << "[DEBUG] Accumulating " << input.size() << " rows" << std::endl;

		// Expect at least 2 columns: timestamp and value
		if (input.ColumnCount() < 2) {
			throw InvalidInputException("FORECAST requires at least 2 input columns (timestamp, value)");
		}

		// Read timestamp and value columns
		auto timestamp_col_idx = gstate.timestamp_col_idx;
		auto value_col_idx = gstate.value_col_idx;

		for (idx_t i = 0; i < input.size(); i++) {
			auto ts_val = input.data[timestamp_col_idx].GetValue(i);
			auto val = input.data[value_col_idx].GetValue(i);

			if (!ts_val.IsNull() && !val.IsNull()) {
				// Convert timestamp
				auto ts_micros = ts_val.GetValue<timestamp_t>();
				auto duration = std::chrono::microseconds(ts_micros.value);
				auto time_point = std::chrono::system_clock::time_point(duration);

				state.timestamps.push_back(time_point);
				state.values.push_back(val.GetValue<double>());

				if (i < 3) { // Log first few for debugging
					         // std::cerr << "[DEBUG]   Row " << i << ": ts=" << ts_val.ToString()
					         //           << ", val=" << val.ToString() << std::endl;
				}
			}
		}

		// std::cerr << "[DEBUG] Total accumulated: " << state.timestamps.size() << " points" << std::endl;
	}

	// Signal we need more input (we'll process in the finalize function)
	return OperatorResultType::NEED_MORE_INPUT;
}

// Finalize: called when all input for this group is processed
OperatorFinalizeResultType ForecastInOutFinal(ExecutionContext &context, TableFunctionInput &data_p,
                                              DataChunk &output) {
	// std::cerr << "[DEBUG] ForecastInOutFinal called" << std::endl;

	auto &state = data_p.local_state->Cast<ForecastLocalState>();
	auto &gstate = data_p.global_state->Cast<ForecastGlobalState>();
	auto &bind_data = *gstate.bind_data;

	// Generate forecast if not done yet
	if (!state.forecast_generated) {
		// std::cerr << "[DEBUG] Generating forecast from " << state.timestamps.size() << " data points" << std::endl;

		if (state.timestamps.empty()) {
			// std::cerr << "[DEBUG] No data - returning empty result" << std::endl;
			output.SetCardinality(0);
			return OperatorFinalizeResultType::FINISHED;
		}

		// Build time series
		auto ts_ptr = TimeSeriesBuilder::BuildTimeSeries(state.timestamps, state.values);

		// Create and fit model
		auto model_ptr = ModelFactory::Create(bind_data.model_name, bind_data.model_params);
		state.model = model_ptr.release();

		state.fit_start_time = std::chrono::high_resolution_clock::now();
		AnofoxTimeWrapper::FitModel(state.model, *ts_ptr);

		// Generate forecast
		auto forecast_ptr = AnofoxTimeWrapper::Predict(state.model, bind_data.horizon);
		state.forecast = forecast_ptr.release();

		state.forecast_generated = true;
		state.output_offset = 0;

		// std::cerr << "[DEBUG] Forecast generated: "
		//           << AnofoxTimeWrapper::GetForecastHorizon(*state.forecast) << " points" << std::endl;
	}

	// Output forecast rows
	idx_t remaining = bind_data.horizon - state.output_offset;
	if (remaining == 0) {
		// std::cerr << "[DEBUG] All forecast rows output, finished" << std::endl;
		return OperatorFinalizeResultType::FINISHED;
	}

	idx_t chunk_size = std::min(remaining, (idx_t)STANDARD_VECTOR_SIZE);

	// std::cerr << "[DEBUG] Outputting " << chunk_size << " rows (offset=" << state.output_offset << ")" << std::endl;

	auto &primary_forecast = AnofoxTimeWrapper::GetPrimaryForecast(*state.forecast);

	idx_t output_count = 0;
	for (idx_t i = 0; i < chunk_size; i++) {
		idx_t forecast_idx = state.output_offset + i;

		// forecast_step (1-indexed)
		output.data[0].SetValue(output_count, Value::INTEGER(forecast_idx + 1));

		// point_forecast
		double point_forecast = primary_forecast[forecast_idx];
		output.data[1].SetValue(output_count, Value::DOUBLE(point_forecast));

		// lower_95 and upper_95
		output.data[2].SetValue(output_count, Value::DOUBLE(point_forecast * 0.9));
		output.data[3].SetValue(output_count, Value::DOUBLE(point_forecast * 1.1));

		// model_name
		output.data[4].SetValue(output_count, Value(AnofoxTimeWrapper::GetModelName(*state.model)));

		// fit_time_ms
		auto end_time = std::chrono::high_resolution_clock::now();
		auto fit_time = std::chrono::duration<double, std::milli>(end_time - state.fit_start_time).count();
		output.data[5].SetValue(output_count, Value::DOUBLE(fit_time));

		output_count++;
	}

	output.SetCardinality(output_count);
	state.output_offset += output_count;

	// std::cerr << "[DEBUG] Output " << output_count << " rows, new offset: " << state.output_offset << std::endl;

	if (state.output_offset >= (idx_t)bind_data.horizon) {
		return OperatorFinalizeResultType::FINISHED;
	}

	return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

unique_ptr<NodeStatistics> ForecastCardinality(ClientContext &context, const FunctionData *bind_data) {
	auto &forecast_bind = bind_data->Cast<ForecastBindData>();
	auto cardinality = forecast_bind.horizon;

	// std::cerr << "[DEBUG] ForecastCardinality: " << cardinality << " rows" << std::endl;
	return make_uniq<NodeStatistics>(cardinality);
}

unique_ptr<TableFunction> CreateForecastTableFunction() {
	// std::cerr << "[DEBUG] CreateForecastTableFunction called" << std::endl;

	// Table-in-out function arguments: just the function parameters (model, horizon, params)
	// The input table columns are provided automatically via the input DataChunk
	vector<LogicalType> arguments = {
	    LogicalType::VARCHAR, // model
	    LogicalType::INTEGER, // horizon
	    LogicalType::ANY      // model_params (optional)
	};

	// Create table function with nullptr for regular function (we use in_out_function)
	TableFunction table_function(arguments, nullptr, ForecastBind, ForecastInitGlobal, ForecastInitLocal);

	// Set in-out handlers
	table_function.in_out_function = ForecastInOutFunction;
	table_function.in_out_function_final = ForecastInOutFinal;
	table_function.cardinality = ForecastCardinality;
	table_function.name = "forecast";

	// std::cerr << "[DEBUG] FORECAST table function created (table-in-out mode)" << std::endl;
	return make_uniq<TableFunction>(table_function);
}

} // namespace duckdb
