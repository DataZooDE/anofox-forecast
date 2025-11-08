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
	std::chrono::microseconds time_update {0};
	std::chrono::microseconds time_sort {0};
	std::chrono::microseconds time_convert {0};
	std::chrono::microseconds time_build_ts {0};
	std::chrono::microseconds time_fit {0};
	std::chrono::microseconds time_predict {0};
	std::chrono::microseconds time_result {0};
	std::chrono::microseconds time_total {0};

	// Memory tracking
	size_t copy_count {0};
	size_t bytes_copied {0};
	size_t peak_capacity {0};
};

// State for the TS_FORECAST aggregate - simple POD type with pointer to data
struct ForecastAggregateState {
	ForecastData *data; // Heap-allocated data

	ForecastAggregateState() : data(nullptr) {
	}
};

// Bind data for the aggregate
struct ForecastAggregateBindData : public FunctionData {
	string model_name;
	int32_t horizon;
	Value model_params;
	double confidence_level;
	bool return_insample;
	bool include_forecast_step;
	string date_col_name;
	string lower_col_name;      // Dynamic column name based on confidence level (e.g., "lower_90")
	string upper_col_name;      // Dynamic column name based on confidence level (e.g., "upper_90")
	LogicalTypeId date_type_id; // Track input date type (INTEGER, DATE, or TIMESTAMP)

	explicit ForecastAggregateBindData(string model, int32_t h, Value params, double conf = 0.90, bool insample = false,
	                                   bool include_step = true, string date_name = "date", string lower_name = "lower",
	                                   string upper_name = "upper", LogicalTypeId date_type = LogicalTypeId::TIMESTAMP)
	    : model_name(std::move(model)), horizon(h), model_params(std::move(params)), confidence_level(conf),
	      return_insample(insample), include_forecast_step(include_step), date_col_name(std::move(date_name)),
	      lower_col_name(std::move(lower_name)), upper_col_name(std::move(upper_name)), date_type_id(date_type) {
	}

	unique_ptr<FunctionData> Copy() const override {
		return make_uniq<ForecastAggregateBindData>(model_name, horizon, model_params, confidence_level,
		                                            return_insample, include_forecast_step, date_col_name,
		                                            lower_col_name, upper_col_name, date_type_id);
	}

	bool Equals(const FunctionData &other_p) const override {
		auto &other = other_p.Cast<ForecastAggregateBindData>();
		return model_name == other.model_name && horizon == other.horizon &&
		       std::abs(confidence_level - other.confidence_level) < 1e-9 && return_insample == other.return_insample &&
		       include_forecast_step == other.include_forecast_step && date_col_name == other.date_col_name &&
		       lower_col_name == other.lower_col_name && upper_col_name == other.upper_col_name &&
		       date_type_id == other.date_type_id;
	}
};

// Create the TS_FORECAST aggregate function
AggregateFunction CreateTSForecastAggregate();

} // namespace duckdb
