#include "forecast_aggregate.hpp"
#include "model_factory.hpp"
#include "time_series_builder.hpp"
#include "anofox_time_wrapper.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/planner/expression/bound_aggregate_expression.hpp"
#include "duckdb/common/types/value_map.hpp"
#include "duckdb/planner/expression/bound_constant_expression.hpp"
#include <iostream>
#include <algorithm>

// Include full anofox-time types
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include "anofox-time/models/iforecaster.hpp"

namespace duckdb {

// Operation struct that defines Initialize, Combine, Finalize, Destroy
struct ForecastAggregateOperation {
    template <class STATE>
    static void Initialize(STATE &state) {
        // std::cerr << "[DEBUG] Initialize called, allocating data" << std::endl;
        state.data = new ForecastData();
    }
    
    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        // std::cerr << "[DEBUG] Combine: merging " << source.data->timestamp_micros.size() << " points" << std::endl;
        if (!source.data || !target.data) {
            // std::cerr << "[ERROR] Combine called with NULL data!" << std::endl;
            return;
        }
        // Simply append - we'll sort in Finalize
        target.data->timestamp_micros.insert(target.data->timestamp_micros.end(), 
                                            source.data->timestamp_micros.begin(), 
                                            source.data->timestamp_micros.end());
        target.data->values.insert(target.data->values.end(), 
                                   source.data->values.begin(), 
                                   source.data->values.end());
    }
    
    // VoidFinalize version - correct signature with STATE template parameter
    template <class STATE>
    static void Finalize(STATE &state, AggregateFinalizeData &finalize_data) {
        // std::cerr << "[DEBUG] Finalize START" << std::endl;
        
        if (!state.data || state.data->timestamp_micros.empty()) {
            // std::cerr << "[DEBUG] Empty state, returning NULL" << std::endl;
            finalize_data.ReturnNull();
            return;
        }
        
        // std::cerr << "[DEBUG] Processing " << state.data->timestamp_micros.size() << " points" << std::endl;
        
        // std::cerr << "[DEBUG] Getting bind data..." << std::endl;
        // Get bind data to access model parameters
        auto &bind_data = finalize_data.input.bind_data->Cast<ForecastAggregateBindData>();
        
        // std::cerr << "[DEBUG] Generating forecast with model: " << bind_data.model_name 
        //           << ", horizon: " << bind_data.horizon << std::endl;
        
        // std::cerr << "[DEBUG] Sorting timestamps..." << std::endl;
        // IMPORTANT: Sort data by timestamp (GROUP BY doesn't guarantee order)
        // Create pairs of (timestamp, value) and sort by timestamp
        vector<std::pair<int64_t, double>> time_value_pairs;
        time_value_pairs.reserve(state.data->timestamp_micros.size());
        for (size_t i = 0; i < state.data->timestamp_micros.size(); i++) {
            time_value_pairs.emplace_back(state.data->timestamp_micros[i], state.data->values[i]);
        }
        
        // Sort by timestamp
        // std::cerr << "[DEBUG] Before sort: " << time_value_pairs.size() << " pairs" << std::endl;
        std::sort(time_value_pairs.begin(), time_value_pairs.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        // std::cerr << "[DEBUG] After sort, removing duplicates..." << std::endl;
        
        // Remove duplicate timestamps (keep first occurrence)
        auto last = std::unique(time_value_pairs.begin(), time_value_pairs.end(),
                               [](const auto& a, const auto& b) { return a.first == b.first; });
        time_value_pairs.erase(last, time_value_pairs.end());
        // std::cerr << "[DEBUG] After dedup: " << time_value_pairs.size() << " unique pairs" << std::endl;
        
        // Convert sorted timestamps to TimePoint and extract values
        vector<std::chrono::system_clock::time_point> timestamps;
        vector<double> sorted_values;
        timestamps.reserve(time_value_pairs.size());
        sorted_values.reserve(time_value_pairs.size());
        
        for (const auto& pair : time_value_pairs) {
            auto duration = std::chrono::microseconds(pair.first);
            timestamps.push_back(std::chrono::system_clock::time_point(duration));
            sorted_values.push_back(pair.second);
        }
        
        // Build time series with sorted data
        // std::cerr << "[DEBUG] Building time series..." << std::endl;
        auto ts_ptr = TimeSeriesBuilder::BuildTimeSeries(timestamps, sorted_values);
        
        // std::cerr << "[DEBUG] Creating model: " << bind_data.model_name << std::endl;
        // Create and fit model
        auto model_ptr = ModelFactory::Create(bind_data.model_name, bind_data.model_params);
        // std::cerr << "[DEBUG] Fitting model..." << std::endl;
        AnofoxTimeWrapper::FitModel(model_ptr.get(), *ts_ptr);
        // std::cerr << "[DEBUG] Model fitted successfully" << std::endl;
        
        // Generate forecast with confidence intervals (95% by default)
        auto forecast_ptr = AnofoxTimeWrapper::PredictWithConfidence(model_ptr.get(), bind_data.horizon, 0.95);
        auto &primary_forecast = AnofoxTimeWrapper::GetPrimaryForecast(*forecast_ptr);
        
        // Calculate time interval from training data for forecast timestamps
        int64_t interval_micros = 0;
        if (timestamps.size() >= 2) {
            // Calculate median interval to handle irregular spacing
            vector<int64_t> intervals;
            for (size_t i = 1; i < timestamps.size(); i++) {
                auto diff = std::chrono::duration_cast<std::chrono::microseconds>(
                    timestamps[i] - timestamps[i-1]).count();
                intervals.push_back(diff);
            }
            // Use median interval for robustness
            std::sort(intervals.begin(), intervals.end());
            interval_micros = intervals[intervals.size() / 2];
        }
        
        // Last timestamp from training data
        int64_t last_timestamp_micros = time_value_pairs.back().first;
        
        // Create result struct with forecast arrays
        child_list_t<Value> struct_values;
        
        vector<Value> steps, forecasts, lowers, uppers, forecast_timestamps;
        
        // Check if model provides prediction intervals
        bool has_intervals = AnofoxTimeWrapper::HasLowerBound(*forecast_ptr) && 
                            AnofoxTimeWrapper::HasUpperBound(*forecast_ptr);
        
        if (has_intervals) {
            // // std::cerr << "[DEBUG] Using model's prediction intervals" << std::endl;
            auto &lower_bound = AnofoxTimeWrapper::GetLowerBound(*forecast_ptr);
            auto &upper_bound = AnofoxTimeWrapper::GetUpperBound(*forecast_ptr);
            
            for (int32_t h = 0; h < bind_data.horizon; h++) {
                steps.push_back(Value::INTEGER(h + 1));
                forecasts.push_back(Value::DOUBLE(primary_forecast[h]));
                lowers.push_back(Value::DOUBLE(lower_bound[h]));
                uppers.push_back(Value::DOUBLE(upper_bound[h]));
                
                // Generate forecast timestamp
                int64_t forecast_ts_micros = last_timestamp_micros + interval_micros * (h + 1);
                forecast_timestamps.push_back(Value::TIMESTAMP(timestamp_t(forecast_ts_micros)));
            }
        } else {
            // // std::cerr << "[DEBUG] Model doesn't provide intervals, using ±10% fallback" << std::endl;
            for (int32_t h = 0; h < bind_data.horizon; h++) {
                steps.push_back(Value::INTEGER(h + 1));
                forecasts.push_back(Value::DOUBLE(primary_forecast[h]));
                lowers.push_back(Value::DOUBLE(primary_forecast[h] * 0.9));
                uppers.push_back(Value::DOUBLE(primary_forecast[h] * 1.1));
                
                // Generate forecast timestamp
                int64_t forecast_ts_micros = last_timestamp_micros + interval_micros * (h + 1);
                forecast_timestamps.push_back(Value::TIMESTAMP(timestamp_t(forecast_ts_micros)));
            }
        }
        
        struct_values.push_back(make_pair("forecast_step", Value::LIST(LogicalType::INTEGER, steps)));
        struct_values.push_back(make_pair("forecast_timestamp", Value::LIST(LogicalType::TIMESTAMP, forecast_timestamps)));
        struct_values.push_back(make_pair("point_forecast", Value::LIST(LogicalType::DOUBLE, forecasts)));
        struct_values.push_back(make_pair("lower_95", Value::LIST(LogicalType::DOUBLE, lowers)));
        struct_values.push_back(make_pair("upper_95", Value::LIST(LogicalType::DOUBLE, uppers)));
        struct_values.push_back(make_pair("model_name", Value(AnofoxTimeWrapper::GetModelName(*model_ptr))));
        
        // Set the result value in the Vector
        auto result_value = Value::STRUCT(std::move(struct_values));
        finalize_data.result.SetValue(finalize_data.result_idx, result_value);
        
        // // std::cerr << "[DEBUG] Finalize complete" << std::endl;
    }
    
    template <class STATE>
    static void Destroy(STATE &state, AggregateInputData &) {
        // std::cerr << "[DEBUG] Destroy called" << std::endl;
        if (state.data) {
            delete state.data;
            state.data = nullptr;
        }
    }
};

// Update function: accumulate timestamp-value pairs
static void TSForecastUpdate(Vector inputs[], AggregateInputData &aggr_input, idx_t input_count,
                             Vector &state_vector, idx_t count) {
    // std::cerr << "[DEBUG] TSForecastUpdate with " << count << " rows, " << input_count << " inputs" << std::endl;
    
    // inputs[0] = timestamp column
    // inputs[1] = value column
    // inputs[2] = model name (constant) - skip, handled in bind
    // inputs[3] = horizon (constant) - skip, handled in bind  
    // inputs[4] = model_params (constant, optional) - skip, handled in bind
    
    // std::cerr << "[DEBUG] Converting inputs to unified format..." << std::endl;
    UnifiedVectorFormat ts_format, val_format;
    inputs[0].ToUnifiedFormat(count, ts_format);
    inputs[1].ToUnifiedFormat(count, val_format);
    
    // std::cerr << "[DEBUG] Getting state pointers from FLAT vector..." << std::endl;
    // State vector is FLAT - use FlatVector::GetData
    auto states = FlatVector::GetData<ForecastAggregateState*>(state_vector);
    
    // std::cerr << "[DEBUG] Processing " << count << " rows..." << std::endl;
    for (idx_t i = 0; i < count; i++) {
        auto ts_idx = ts_format.sel->get_index(i);
        auto val_idx = val_format.sel->get_index(i);
        
        if (i < 2) {  // Only print for first 2 rows
            // std::cerr << "[DEBUG] Row " << i << ": ts_idx=" << ts_idx << ", val_idx=" << val_idx << std::endl;
            // std::cerr << "[DEBUG] states[" << i << "] = " << (void*)states[i] << std::endl;
        }
        
        if (!ts_format.validity.RowIsValid(ts_idx) || !val_format.validity.RowIsValid(val_idx)) {
            continue;
        }
        
        // Each row updates its own state (state_vector maps rows to groups)
        auto &state = *states[i];
        
        if (!state.data) {
            // std::cerr << "[ERROR] Row " << i << ": state.data is NULL!" << std::endl;
            continue;
        }
        
        // Extract timestamp as microseconds
        auto ts_val = UnifiedVectorFormat::GetData<timestamp_t>(ts_format)[ts_idx];
        if (i < 2) {
            // std::cerr << "[DEBUG] Row " << i << ": ts_val=" << ts_val.value << ", pushing to state at " << (void*)state.data << std::endl;
        }
        state.data->timestamp_micros.push_back(ts_val.value);
        
        // Extract value
        auto value = UnifiedVectorFormat::GetData<double>(val_format)[val_idx];
        if (i < 2) {
            // std::cerr << "[DEBUG] Row " << i << ": value=" << value << std::endl;
        }
        state.data->values.push_back(value);
        
        if (i < 2) {
            // std::cerr << "[DEBUG] Row " << i << ": state.data now has " << state.data->timestamp_micros.size() << " points" << std::endl;
        }
    }
    
    // std::cerr << "[DEBUG] Update complete, processed " << count << " rows" << std::endl;
}

// Bind function: extract constant parameters
unique_ptr<FunctionData> TSForecastBind(ClientContext &context, AggregateFunction &function,
                                       vector<unique_ptr<Expression>> &arguments) {
    // // std::cerr << "[DEBUG] TSForecastBind with " << arguments.size() << " arguments" << std::endl;
    
    if (arguments.size() < 4) {
        throw BinderException("TS_FORECAST requires at least 4 arguments: timestamp, value, model, horizon");
    }
    
    // Extract constant values for model and horizon
    string model_name = "Naive";
    int32_t horizon = 1;
    Value model_params = Value::STRUCT({});
    
    // Try to extract model name (argument 2)
    if (arguments[2]->IsFoldable()) {
        auto model_val = ExpressionExecutor::EvaluateScalar(context, *arguments[2]);
        model_name = model_val.GetValue<string>();
        // // std::cerr << "[DEBUG] Extracted model: " << model_name << std::endl;
    }
    
    // Try to extract horizon (argument 3)
    if (arguments[3]->IsFoldable()) {
        auto horizon_val = ExpressionExecutor::EvaluateScalar(context, *arguments[3]);
        horizon = horizon_val.GetValue<int32_t>();
        // // std::cerr << "[DEBUG] Extracted horizon: " << horizon << std::endl;
    }
    
    // Try to extract model params (argument 4, optional)
    if (arguments.size() > 4 && arguments[4]->IsFoldable()) {
        model_params = ExpressionExecutor::EvaluateScalar(context, *arguments[4]);
    }
    
    // Validate
    if (horizon <= 0) {
        throw BinderException("Horizon must be positive");
    }
    
    auto supported = ModelFactory::GetSupportedModels();
    if (std::find(supported.begin(), supported.end(), model_name) == supported.end()) {
        throw BinderException("Unsupported model: " + model_name);
    }
    
    ModelFactory::ValidateModelParams(model_name, model_params);
    
    // Set return type
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("forecast_step", LogicalType::LIST(LogicalType::INTEGER)));
    struct_children.push_back(make_pair("forecast_timestamp", LogicalType::LIST(LogicalType::TIMESTAMP)));
    struct_children.push_back(make_pair("point_forecast", LogicalType::LIST(LogicalType::DOUBLE)));
    struct_children.push_back(make_pair("lower_95", LogicalType::LIST(LogicalType::DOUBLE)));
    struct_children.push_back(make_pair("upper_95", LogicalType::LIST(LogicalType::DOUBLE)));
    struct_children.push_back(make_pair("model_name", LogicalType::VARCHAR));
    
    function.return_type = LogicalType::STRUCT(std::move(struct_children));
    
    // // std::cerr << "[DEBUG] TSForecastBind complete" << std::endl;
    return make_uniq<ForecastAggregateBindData>(model_name, horizon, model_params);
}

AggregateFunction CreateTSForecastAggregate() {
    // // std::cerr << "[DEBUG] CreateTSForecastAggregate called" << std::endl;
    
    using STATE = ForecastAggregateState;
    using OP = ForecastAggregateOperation;
    
    // Create aggregate function - use LEGACY destructor for non-trivial state
    AggregateFunction forecast_agg(
        "ts_forecast",  // name
        {LogicalType::TIMESTAMP, LogicalType::DOUBLE, LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::ANY},  // arguments
        LogicalType::STRUCT({}),  // return_type (will be set in bind)
        AggregateFunction::StateSize<STATE>,  // state_size
        AggregateFunction::StateInitialize<STATE, OP, AggregateDestructorType::LEGACY>,  // initialize
        TSForecastUpdate,  // update
        AggregateFunction::StateCombine<STATE, OP>,  // combine
        AggregateFunction::StateVoidFinalize<STATE, OP>,  // finalize
        nullptr,  // simple_update
        TSForecastBind,  // bind
        AggregateFunction::StateDestroy<STATE, OP>  // destructor
    );
    
    // // std::cerr << "[DEBUG] TS_FORECAST aggregate created" << std::endl;
    return forecast_agg;
}

} // namespace duckdb