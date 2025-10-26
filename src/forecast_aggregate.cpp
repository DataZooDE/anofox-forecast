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
#include <chrono>

// Performance profiling utility - RAII timer
class ScopedTimer {
    std::chrono::high_resolution_clock::time_point start_;
    std::chrono::microseconds* output_;
public:
    explicit ScopedTimer(std::chrono::microseconds* out) 
        : start_(std::chrono::high_resolution_clock::now()), output_(out) {}
    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        *output_ = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);
    }
};

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
        auto total_start = std::chrono::high_resolution_clock::now();
        
        if (!state.data || state.data->timestamp_micros.empty()) {
            finalize_data.ReturnNull();
            return;
        }
        
        auto &bind_data = finalize_data.input.bind_data->Cast<ForecastAggregateBindData>();
        
        // Track memory: count data copies
        state.data->copy_count = 0;
        state.data->bytes_copied = 0;
        size_t data_size = state.data->timestamp_micros.size();
        
        // STAGE 1: SORT + DEDUP
        vector<std::pair<int64_t, double>> time_value_pairs;
        time_value_pairs.reserve(data_size);
        {
            ScopedTimer timer(&state.data->time_sort);
            
            // Copy 1: Create time_value_pairs
            for (size_t i = 0; i < data_size; i++) {
                time_value_pairs.emplace_back(state.data->timestamp_micros[i], state.data->values[i]);
            }
            state.data->copy_count++;
            state.data->bytes_copied += data_size * sizeof(std::pair<int64_t, double>);
            
            // Sort by timestamp
            std::sort(time_value_pairs.begin(), time_value_pairs.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            
            // Remove duplicate timestamps
            auto last = std::unique(time_value_pairs.begin(), time_value_pairs.end(),
                                   [](const auto& a, const auto& b) { return a.first == b.first; });
            time_value_pairs.erase(last, time_value_pairs.end());
        }
        
        // STAGE 2: CONVERT TO TIMEPOINTS
        vector<std::chrono::system_clock::time_point> timestamps;
        vector<double> sorted_values;
        timestamps.reserve(time_value_pairs.size());
        sorted_values.reserve(time_value_pairs.size());
        {
            ScopedTimer timer(&state.data->time_convert);
            
            // Copy 2: Split pairs into separate vectors
            for (const auto& pair : time_value_pairs) {
                auto duration = std::chrono::microseconds(pair.first);
                timestamps.push_back(std::chrono::system_clock::time_point(duration));
                sorted_values.push_back(pair.second);
            }
            state.data->copy_count++;
            state.data->bytes_copied += time_value_pairs.size() * (sizeof(std::chrono::system_clock::time_point) + sizeof(double));
        }
        
        // STAGE 3: BUILD TIMESERIES
        std::unique_ptr<::anofoxtime::core::TimeSeries> ts_ptr;
        {
            ScopedTimer timer(&state.data->time_build_ts);
            // Copy 3: Pass vectors to TimeSeries (by value, then moved internally)
            ts_ptr = TimeSeriesBuilder::BuildTimeSeries(timestamps, sorted_values);
            state.data->copy_count++;
            state.data->bytes_copied += timestamps.size() * (sizeof(std::chrono::system_clock::time_point) + sizeof(double));
        }
        
        // STAGE 4: FIT MODEL
        std::unique_ptr<::anofoxtime::models::IForecaster> model_ptr;
        {
            ScopedTimer timer(&state.data->time_fit);
            model_ptr = ModelFactory::Create(bind_data.model_name, bind_data.model_params);
            AnofoxTimeWrapper::FitModel(model_ptr.get(), *ts_ptr);
        }
        
        // STAGE 5: PREDICT
        std::unique_ptr<::anofoxtime::core::Forecast> forecast_ptr;
        {
            ScopedTimer timer(&state.data->time_predict);
            forecast_ptr = AnofoxTimeWrapper::PredictWithConfidence(model_ptr.get(), bind_data.horizon, bind_data.confidence_level);
        }
        auto &primary_forecast = AnofoxTimeWrapper::GetPrimaryForecast(*forecast_ptr);
        
        // STAGE 5.5: CALCULATE FORECAST TIMESTAMPS
        std::chrono::microseconds time_timestamp_calc{0};
        int64_t interval_micros = 0;
        int64_t last_timestamp_micros = 0;
        bool generate_timestamps = true;  // Default: enabled
        
        // Check if user disabled timestamp generation
        if (bind_data.model_params.type().id() == LogicalTypeId::STRUCT) {
            auto &struct_children = StructValue::GetChildren(bind_data.model_params);
            for (size_t i = 0; i < struct_children.size(); i++) {
                auto &key = StructType::GetChildName(bind_data.model_params.type(), i);
                if (key == "generate_timestamps") {
                    generate_timestamps = struct_children[i].GetValue<bool>();
                    break;
                }
            }
        }
        
        if (generate_timestamps) {
            ScopedTimer timer(&time_timestamp_calc);
            
            // Calculate mean interval (O(1) - assumes regular, sorted data)
            if (timestamps.size() >= 2) {
                auto total_time = std::chrono::duration_cast<std::chrono::microseconds>(
                    timestamps.back() - timestamps.front()).count();
                interval_micros = total_time / (timestamps.size() - 1);
            }
            
            // Last timestamp from training data
            last_timestamp_micros = time_value_pairs.back().first;
        }
        
        // STAGE 6: BUILD RESULT
        {
            ScopedTimer timer(&state.data->time_result);
            
            child_list_t<Value> struct_values;
            vector<Value> steps, forecasts, lowers, uppers, forecast_timestamps;
            
            // Check if model provides prediction intervals
            bool has_intervals = AnofoxTimeWrapper::HasLowerBound(*forecast_ptr) && 
                                AnofoxTimeWrapper::HasUpperBound(*forecast_ptr);
            
            if (has_intervals) {
                auto &lower_bound = AnofoxTimeWrapper::GetLowerBound(*forecast_ptr);
                auto &upper_bound = AnofoxTimeWrapper::GetUpperBound(*forecast_ptr);
                
                for (int32_t h = 0; h < bind_data.horizon; h++) {
                    steps.push_back(Value::INTEGER(h + 1));
                    forecasts.push_back(Value::DOUBLE(primary_forecast[h]));
                    lowers.push_back(Value::DOUBLE(lower_bound[h]));
                    uppers.push_back(Value::DOUBLE(upper_bound[h]));
                    
                    if (generate_timestamps) {
                        int64_t forecast_ts_micros = last_timestamp_micros + interval_micros * (h + 1);
                        forecast_timestamps.push_back(Value::TIMESTAMP(timestamp_t(forecast_ts_micros)));
                    }
                }
            } else {
                for (int32_t h = 0; h < bind_data.horizon; h++) {
                    steps.push_back(Value::INTEGER(h + 1));
                    forecasts.push_back(Value::DOUBLE(primary_forecast[h]));
                    lowers.push_back(Value::DOUBLE(primary_forecast[h] * 0.9));
                    uppers.push_back(Value::DOUBLE(primary_forecast[h] * 1.1));
                    
                    if (generate_timestamps) {
                        int64_t forecast_ts_micros = last_timestamp_micros + interval_micros * (h + 1);
                        forecast_timestamps.push_back(Value::TIMESTAMP(timestamp_t(forecast_ts_micros)));
                    }
                }
            }
            
            // Copy 4: Create DuckDB Value::LIST objects
            state.data->copy_count++;
            state.data->bytes_copied += bind_data.horizon * (sizeof(int32_t) + sizeof(double) * 4 + sizeof(timestamp_t));
            
            struct_values.push_back(make_pair("forecast_step", Value::LIST(LogicalType::INTEGER, steps)));
            
            // Include forecast_timestamp field (empty if disabled for schema consistency)
            if (generate_timestamps) {
                struct_values.push_back(make_pair("forecast_timestamp", Value::LIST(LogicalType::TIMESTAMP, forecast_timestamps)));
            } else {
                vector<Value> empty_timestamps;
                struct_values.push_back(make_pair("forecast_timestamp", Value::LIST(LogicalType::TIMESTAMP, empty_timestamps)));
            }
            
            struct_values.push_back(make_pair("point_forecast", Value::LIST(LogicalType::DOUBLE, forecasts)));
            struct_values.push_back(make_pair("lower", Value::LIST(LogicalType::DOUBLE, lowers)));
            struct_values.push_back(make_pair("upper", Value::LIST(LogicalType::DOUBLE, uppers)));
            struct_values.push_back(make_pair("model_name", Value(AnofoxTimeWrapper::GetModelName(*model_ptr))));
            
            auto result_value = Value::STRUCT(std::move(struct_values));
            finalize_data.result.SetValue(finalize_data.result_idx, result_value);
        }
        
        // Calculate total time
        auto total_end = std::chrono::high_resolution_clock::now();
        state.data->time_total = std::chrono::duration_cast<std::chrono::microseconds>(total_end - total_start);
        
        // Print performance profile (enable with environment variable)
        if (std::getenv("ANOFOX_PERF")) {
            auto to_ms = [](std::chrono::microseconds us) { return us.count() / 1000.0; };
            auto pct = [](std::chrono::microseconds part, std::chrono::microseconds total) { 
                return total.count() > 0 ? (100.0 * part.count() / total.count()) : 0.0; 
            };
            
            std::fprintf(stderr, "\n[PERF] Model=%s, Rows=%zu, Horizon=%d\n",
                bind_data.model_name.c_str(), data_size, bind_data.horizon);
            std::fprintf(stderr, "[PERF] Sort:    %6.2fms (%5.1f%%)\n", 
                to_ms(state.data->time_sort), pct(state.data->time_sort, state.data->time_total));
            std::fprintf(stderr, "[PERF] Convert: %6.2fms (%5.1f%%)\n", 
                to_ms(state.data->time_convert), pct(state.data->time_convert, state.data->time_total));
            std::fprintf(stderr, "[PERF] BuildTS: %6.2fms (%5.1f%%)\n", 
                to_ms(state.data->time_build_ts), pct(state.data->time_build_ts, state.data->time_total));
            std::fprintf(stderr, "[PERF] Fit:     %6.2fms (%5.1f%%)\n", 
                to_ms(state.data->time_fit), pct(state.data->time_fit, state.data->time_total));
            std::fprintf(stderr, "[PERF] Predict: %6.2fms (%5.1f%%)\n", 
                to_ms(state.data->time_predict), pct(state.data->time_predict, state.data->time_total));
            std::fprintf(stderr, "[PERF] TsCalc:  %6.2fms (%5.1f%%)\n", 
                to_ms(time_timestamp_calc), pct(time_timestamp_calc, state.data->time_total));
            std::fprintf(stderr, "[PERF] Result:  %6.2fms (%5.1f%%)\n", 
                to_ms(state.data->time_result), pct(state.data->time_result, state.data->time_total));
            std::fprintf(stderr, "[PERF] TOTAL:   %6.2fms\n", to_ms(state.data->time_total));
            std::fprintf(stderr, "[PERF] Copies: %zu, Bytes: %zu KB\n", 
                state.data->copy_count, state.data->bytes_copied / 1024);
        }
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
    auto update_start = std::chrono::high_resolution_clock::now();
    
    // inputs[0] = timestamp column
    // inputs[1] = value column
    // inputs[2] = model name (constant) - skip, handled in bind
    // inputs[3] = horizon (constant) - skip, handled in bind  
    // inputs[4] = model_params (constant, optional) - skip, handled in bind
    
    UnifiedVectorFormat ts_format, val_format;
    inputs[0].ToUnifiedFormat(count, ts_format);
    inputs[1].ToUnifiedFormat(count, val_format);
    
    // State vector is FLAT - use FlatVector::GetData
    auto states = FlatVector::GetData<ForecastAggregateState*>(state_vector);
    
    for (idx_t i = 0; i < count; i++) {
        auto ts_idx = ts_format.sel->get_index(i);
        auto val_idx = val_format.sel->get_index(i);
        
        if (!ts_format.validity.RowIsValid(ts_idx) || !val_format.validity.RowIsValid(val_idx)) {
            continue;
        }
        
        // Each row updates its own state (state_vector maps rows to groups)
        auto &state = *states[i];
        
        if (!state.data) {
            continue;
        }
        
        // Track capacity for reallocation detection
        size_t old_cap = state.data->timestamp_micros.capacity();
        
        // Extract timestamp as microseconds
        auto ts_val = UnifiedVectorFormat::GetData<timestamp_t>(ts_format)[ts_idx];
        state.data->timestamp_micros.push_back(ts_val.value);
        
        // Extract value
        auto value = UnifiedVectorFormat::GetData<double>(val_format)[val_idx];
        state.data->values.push_back(value);
        
        // Track if reallocation happened
        size_t new_cap = state.data->timestamp_micros.capacity();
        if (new_cap > old_cap) {
            state.data->peak_capacity = new_cap;
        }
    }
    
    // Accumulate update time (will be summed across all Update calls for this group)
    auto update_end = std::chrono::high_resolution_clock::now();
    auto update_time = std::chrono::duration_cast<std::chrono::microseconds>(update_end - update_start);
    
    // Add to first non-null state (they all belong to same group in GROUP BY)
    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];
        if (state.data) {
            state.data->time_update += update_time;
            break;
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
    
    // Extract confidence_level parameter (default: 0.90)
    double confidence_level = 0.90;
    if (model_params.type().id() == LogicalTypeId::STRUCT) {
        auto &struct_children = StructValue::GetChildren(model_params);
        for (size_t i = 0; i < struct_children.size(); i++) {
            auto &key = StructType::GetChildName(model_params.type(), i);
            if (key == "confidence_level") {
                confidence_level = struct_children[i].GetValue<double>();
                // Validate confidence level
                if (confidence_level <= 0.0 || confidence_level >= 1.0) {
                    throw BinderException("confidence_level must be between 0 and 1 (got " + 
                                        std::to_string(confidence_level) + ")");
                }
                break;
            }
        }
    }
    
    // Set return type
    child_list_t<LogicalType> struct_children;
    struct_children.push_back(make_pair("forecast_step", LogicalType::LIST(LogicalType::INTEGER)));
    struct_children.push_back(make_pair("forecast_timestamp", LogicalType::LIST(LogicalType::TIMESTAMP)));
    struct_children.push_back(make_pair("point_forecast", LogicalType::LIST(LogicalType::DOUBLE)));
    struct_children.push_back(make_pair("lower", LogicalType::LIST(LogicalType::DOUBLE)));
    struct_children.push_back(make_pair("upper", LogicalType::LIST(LogicalType::DOUBLE)));
    struct_children.push_back(make_pair("model_name", LogicalType::VARCHAR));
    
    function.return_type = LogicalType::STRUCT(std::move(struct_children));
    
    // // std::cerr << "[DEBUG] TSForecastBind complete" << std::endl;
    return make_uniq<ForecastAggregateBindData>(model_name, horizon, model_params, confidence_level);
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