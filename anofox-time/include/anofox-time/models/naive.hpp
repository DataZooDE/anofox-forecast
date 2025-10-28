#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Naive forecasting method (random walk)
 * 
 * The simplest forecasting method: all future values are predicted to be
 * equal to the last observed value. Also known as the "random walk" model.
 * 
 * This is the most fundamental baseline for non-seasonal data.
 */
class Naive : public IForecaster {
public:
	Naive();
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	core::Forecast predictWithConfidence(int horizon, double confidence);
	
	std::string getName() const override {
		return "Naive";
	}
	
	// Accessors
	const std::vector<double>& fittedValues() const {
		return fitted_;
	}
	
	const std::vector<double>& residuals() const {
		return residuals_;
	}
	
	double lastValue() const {
		return last_value_;
	}
	
private:
	double last_value_;
	std::vector<double> history_;
	std::vector<double> fitted_;
	std::vector<double> residuals_;
	bool is_fitted_ = false;
	
	void computeFittedValues();
};

} // namespace anofoxtime::models

