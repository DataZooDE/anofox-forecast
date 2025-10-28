#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Random walk with drift forecasting method
 * 
 * Extends the naive method by adding a linear drift term. Forecasts are
 * computed as the last value plus drift × horizon.
 * 
 * Drift is estimated as the average change per time step across the entire series.
 */
class RandomWalkWithDrift : public IForecaster {
public:
	RandomWalkWithDrift();
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	core::Forecast predictWithConfidence(int horizon, double confidence);
	
	std::string getName() const override {
		return "RandomWalkWithDrift";
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
	
	double drift() const {
		return drift_;
	}
	
private:
	double last_value_;
	double drift_;
	std::vector<double> history_;
	std::vector<double> fitted_;
	std::vector<double> residuals_;
	bool is_fitted_ = false;
	
	void computeDrift();
	void computeFittedValues();
};

} // namespace anofoxtime::models

