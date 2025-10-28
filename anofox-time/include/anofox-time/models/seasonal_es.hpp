#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Seasonal Exponential Smoothing
 * 
 * Exponential smoothing with level and multiplicative seasonal components (no trend).
 * Simpler API than ETS(A,N,M) for common use case.
 * 
 * Equations:
 *   level_t = α × (y_t / s_{t-m}) + (1-α) × level_{t-1}
 *   s_t = γ × (y_t / level_t) + (1-γ) × s_{t-m}
 *   forecast_{t+h} = level_t × s_{t-m+[(h-1) mod m]}
 */
class SeasonalExponentialSmoothing : public IForecaster {
public:
	/**
	 * @brief Construct a Seasonal ES forecaster
	 * @param seasonal_period Seasonal period (e.g., 12 for monthly)
	 * @param alpha Level smoothing parameter [0, 1]
	 * @param gamma Seasonal smoothing parameter [0, 1]
	 */
	SeasonalExponentialSmoothing(int seasonal_period, double alpha, double gamma);
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	core::Forecast predictWithConfidence(int horizon, double confidence);
	
	std::string getName() const override {
		return "SeasonalExponentialSmoothing";
	}
	
	// Accessors
	int seasonalPeriod() const {
		return seasonal_period_;
	}
	
	double alpha() const {
		return alpha_;
	}
	
	double gamma() const {
		return gamma_;
	}
	
	double lastLevel() const {
		return level_;
	}
	
	const std::vector<double>& seasonalIndices() const {
		return seasonal_indices_;
	}
	
	const std::vector<double>& fittedValues() const {
		return fitted_;
	}
	
	const std::vector<double>& residuals() const {
		return residuals_;
	}
	
private:
	int seasonal_period_;
	double alpha_;
	double gamma_;
	double level_;
	std::vector<double> seasonal_indices_;
	std::vector<double> history_;
	std::vector<double> fitted_;
	std::vector<double> residuals_;
	bool is_fitted_ = false;
	
	void initializeSeasonalIndices(const std::vector<double>& data);
	void computeFittedValues();
};

} // namespace anofoxtime::models

