#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/seasonal_es.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <memory>
#include <vector>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Optimized Seasonal Exponential Smoothing
 * 
 * Automatically optimizes alpha and gamma parameters to minimize MSE.
 */
class SeasonalESOptimized : public IForecaster {
public:
	/**
	 * @brief Construct an optimized seasonal ES forecaster
	 * @param seasonal_period Seasonal period (e.g., 12 for monthly)
	 */
	explicit SeasonalESOptimized(int seasonal_period);
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	core::Forecast predictWithConfidence(int horizon, double confidence);
	
	std::string getName() const override {
		return "SeasonalESOptimized";
	}
	
	// Accessors
	double optimalAlpha() const {
		return optimal_alpha_;
	}
	
	double optimalGamma() const {
		return optimal_gamma_;
	}
	
	double optimalMSE() const {
		return optimal_mse_;
	}
	
	const std::vector<double>& fittedValues() const;
	const std::vector<double>& residuals() const;
	
private:
	int seasonal_period_;
	double optimal_alpha_;
	double optimal_gamma_;
	double optimal_mse_;
	std::unique_ptr<SeasonalExponentialSmoothing> fitted_model_;
	bool is_fitted_ = false;
	
	struct OptResult {
		double alpha;
		double gamma;
		double mse;
	};
	
	OptResult optimize(const std::vector<double>& data);
};

} // namespace anofoxtime::models

