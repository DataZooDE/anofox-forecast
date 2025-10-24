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
    
    explicit ForecastAggregateBindData(string model, int32_t h, Value params)
        : model_name(std::move(model)), horizon(h), model_params(std::move(params)) {}
    
    unique_ptr<FunctionData> Copy() const override {
        return make_uniq<ForecastAggregateBindData>(model_name, horizon, model_params);
    }
    
    bool Equals(const FunctionData &other_p) const override {
        auto &other = other_p.Cast<ForecastAggregateBindData>();
        return model_name == other.model_name && horizon == other.horizon;
    }
};

// Create the TS_FORECAST aggregate function
AggregateFunction CreateTSForecastAggregate();

} // namespace duckdb