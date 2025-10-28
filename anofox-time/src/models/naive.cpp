#include "anofox-time/models/naive.hpp"
#include "anofox-time/utils/logging.hpp"
#include <cmath>
#include <stdexcept>
#include <numeric>

namespace anofoxtime::models {

Naive::Naive() : last_value_(0.0) {}

void Naive::computeFittedValues() {
	const size_t n = history_.size();
	fitted_.resize(n);
	residuals_.resize(n);
	
	// First value has no forecast
	fitted_[0] = history_[0];
	residuals_[0] = 0.0;
	
	// For each subsequent value, the fitted value is the previous observation
	for (size_t i = 1; i < n; ++i) {
		fitted_[i] = history_[i - 1];
		residuals_[i] = history_[i] - fitted_[i];
	}
}

void Naive::fit(const core::TimeSeries& ts) {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("Naive currently supports univariate series only");
	}
	
	if (ts.size() == 0) {
		throw std::invalid_argument("Cannot fit Naive on empty time series");
	}
	
	history_ = ts.getValues();
	last_value_ = history_.back();
	
	computeFittedValues();
	
	is_fitted_ = true;
	
	ANOFOX_INFO("Naive model fitted with {} data points, last value = {:.4f}", 
	            history_.size(), last_value_);
}

core::Forecast Naive::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("Naive::predict called before fit");
	}
	
	if (horizon <= 0) {
		return {};
	}
	
	// All forecasts are the last observed value
	std::vector<double> forecast(horizon, last_value_);
	
	core::Forecast result;
	result.primary() = forecast;
	return result;
}

core::Forecast Naive::predictWithConfidence(int horizon, double confidence) {
	if (confidence <= 0.0 || confidence >= 1.0) {
		throw std::invalid_argument("Confidence level must be between 0 and 1");
	}
	
	auto forecast = predict(horizon);
	
	// Compute residual variance
	if (residuals_.empty() || residuals_.size() < 2) {
		return forecast;
	}
	
	double sum_sq = 0.0;
	for (size_t i = 1; i < residuals_.size(); ++i) {  // Skip first (no forecast)
		sum_sq += residuals_[i] * residuals_[i];
	}
	const double sigma = std::sqrt(sum_sq / static_cast<double>(residuals_.size() - 1));
	
	// Normal quantile for confidence interval (approx for 95%)
	const double z = 1.96;
	
	// Variance grows linearly with horizon for random walk
	auto& lower = forecast.lowerSeries();
	auto& upper = forecast.upperSeries();
	lower.resize(horizon);
	upper.resize(horizon);
	
	for (int h = 0; h < horizon; ++h) {
		const double std_h = sigma * std::sqrt(static_cast<double>(h + 1));
		lower[h] = forecast.primary()[h] - z * std_h;
		upper[h] = forecast.primary()[h] + z * std_h;
	}
	
	return forecast;
}

} // namespace anofoxtime::models

