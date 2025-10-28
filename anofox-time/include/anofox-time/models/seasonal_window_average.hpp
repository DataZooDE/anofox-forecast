#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Seasonal Window Average forecasting method
 * 
 * Forecasts are computed as the average of the last k observations from
 * the same season. This smooths out noise compared to SeasonalNaive.
 * 
 * For example, with monthly data (s=12) and window=3, January forecast
 * is the mean of the last 3 Januarys.
 */
class SeasonalWindowAverage : public IForecaster {
public:
	/**
	 * @brief Construct a SeasonalWindowAverage forecaster
	 * @param seasonal_period Seasonal period (e.g., 12 for monthly, 4 for quarterly)
	 * @param window Number of past seasonal values to average (default: 2)
	 */
	explicit SeasonalWindowAverage(int seasonal_period, int window = 2);
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	core::Forecast predictWithConfidence(int horizon, double confidence);
	
	std::string getName() const override {
		return "SeasonalWindowAverage";
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
	
	int window() const {
		return window_;
	}
	
private:
	int seasonal_period_;
	int window_;
	std::vector<double> history_;
	std::vector<double> fitted_;
	std::vector<double> residuals_;
	bool is_fitted_ = false;
	
	void computeFittedValues();
	double computeSeasonalAverage(size_t target_idx, int window_size) const;
};

} // namespace anofoxtime::models

