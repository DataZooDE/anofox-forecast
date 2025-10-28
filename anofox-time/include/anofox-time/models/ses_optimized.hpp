#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/ses.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <memory>
#include <vector>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Optimized Simple Exponential Smoothing
 * 
 * Automatically optimizes the alpha parameter to minimize MSE on
 * one-step-ahead forecasts.
 */
class SESOptimized : public IForecaster {
public:
	SESOptimized();
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	core::Forecast predictWithConfidence(int horizon, double confidence);
	
	std::string getName() const override {
		return "SESOptimized";
	}
	
	// Accessors
	double optimalAlpha() const {
		return optimal_alpha_;
	}
	
	double optimalMSE() const {
		return optimal_mse_;
	}
	
	const std::vector<double>& fittedValues() const;
	const std::vector<double>& residuals() const;
	
private:
	double optimal_alpha_;
	double optimal_mse_;
	std::unique_ptr<SimpleExponentialSmoothing> fitted_model_;
	bool is_fitted_ = false;
	
	double optimizeAlpha(const std::vector<double>& data);
};

} // namespace anofoxtime::models

