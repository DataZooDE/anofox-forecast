#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Seasonal Naive forecasting method
 * 
 * Forecasts are set to the last observed value from the same season.
 * For example, with monthly data (s=12), January forecast = last January,
 * February forecast = last February, etc.
 * 
 * This is often the best baseline for seasonal data and is hard to beat
 * with simple methods.
 */
class SeasonalNaive : public IForecaster {
public:
	/**
	 * @brief Construct a SeasonalNaive forecaster
	 * @param seasonal_period Seasonal period (e.g., 12 for monthly, 4 for quarterly)
	 */
	explicit SeasonalNaive(int seasonal_period);
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	core::Forecast predictWithConfidence(int horizon, double confidence);
	
	std::string getName() const override {
		return "SeasonalNaive";
	}
	
	// Accessors
	const std::vector<double>& fittedValues() const {
		return fitted_;
	}
	
	const std::vector<double>& residuals() const {
		return residuals_;
	}
	
	int seasonalPeriod() const {
		return seasonal_period_;
	}
	
private:
	int seasonal_period_;
	std::vector<double> history_;
	std::vector<double> fitted_;
	std::vector<double> residuals_;
	bool is_fitted_ = false;
	
	void computeFittedValues();
};

} // namespace anofoxtime::models

