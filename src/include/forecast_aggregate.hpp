#pragma once

#include "duckdb.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include <memory>
#include <vector>
#include <chrono>

namespace duckdb {

// Data container for forecast aggregate (heap-allocated)
struct ForecastData {
    vector<int64_t> timestamp_micros;
    vector<double> values;
    
    // Performance timing breakdowns (in microseconds)
    std::chrono::microseconds time_update{0};
    std::chrono::microseconds time_sort{0};
    std::chrono::microseconds time_convert{0};
    std::chrono::microseconds time_build_ts{0};
    std::chrono::microseconds time_fit{0};
    std::chrono::microseconds time_predict{0};
    std::chrono::microseconds time_result{0};
    std::chrono::microseconds time_total{0};
    
    // Memory tracking
    size_t copy_count{0};
    size_t bytes_copied{0};
    size_t peak_capacity{0};
};

// State for the TS_FORECAST aggregate - simple POD type with pointer to data
struct ForecastAggregateState {
    ForecastData *data;  // Heap-allocated data
    
    ForecastAggregateState() : data(nullptr) {}
};

// Bind data for the aggregate
struct ForecastAggregateBindData : public FunctionData {
    string model_name;
    int32_t horizon;
    Value model_params;
    double confidence_level;
    
    explicit ForecastAggregateBindData(string model, int32_t h, Value params, double conf = 0.90)
        : model_name(std::move(model)), horizon(h), model_params(std::move(params)), confidence_level(conf) {}
    
    unique_ptr<FunctionData> Copy() const override {
        return make_uniq<ForecastAggregateBindData>(model_name, horizon, model_params, confidence_level);
    }
    
    bool Equals(const FunctionData &other_p) const override {
        auto &other = other_p.Cast<ForecastAggregateBindData>();
        return model_name == other.model_name && horizon == other.horizon && 
               std::abs(confidence_level - other.confidence_level) < 1e-9;
    }
};

// Create the TS_FORECAST aggregate function
AggregateFunction CreateTSForecastAggregate();

} // namespace duckdb