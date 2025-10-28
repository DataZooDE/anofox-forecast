#include "anofox-time/models/random_walk_drift.hpp"
#include "anofox-time/utils/logging.hpp"
#include <cmath>
#include <stdexcept>
#include <numeric>

namespace anofoxtime::models {

RandomWalkWithDrift::RandomWalkWithDrift() : last_value_(0.0), drift_(0.0) {}

void RandomWalkWithDrift::computeDrift() {
	if (history_.size() < 2) {
		drift_ = 0.0;
		return;
	}
	
	// Average change per time step
	drift_ = (history_.back() - history_.front()) / static_cast<double>(history_.size() - 1);
}

void RandomWalkWithDrift::computeFittedValues() {
	const size_t n = history_.size();
	fitted_.resize(n);
	residuals_.resize(n);
	
	// First value has no forecast
	fitted_[0] = history_[0];
	residuals_[0] = 0.0;
	
	// For each subsequent value, the fitted value is previous value + drift
	for (size_t i = 1; i < n; ++i) {
		fitted_[i] = history_[i - 1] + drift_;
		residuals_[i] = history_[i] - fitted_[i];
	}
}

void RandomWalkWithDrift::fit(const core::TimeSeries& ts) {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("RandomWalkWithDrift currently supports univariate series only");
	}
	
	if (ts.size() == 0) {
		throw std::invalid_argument("Cannot fit RandomWalkWithDrift on empty time series");
	}
	
	history_ = ts.getValues();
	last_value_ = history_.back();
	
	computeDrift();
	computeFittedValues();
	
	is_fitted_ = true;
	
	ANOFOX_INFO("RandomWalkWithDrift model fitted with {} data points, drift = {:.4f}", 
	            history_.size(), drift_);
}

core::Forecast RandomWalkWithDrift::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("RandomWalkWithDrift::predict called before fit");
	}
	
	if (horizon <= 0) {
		return {};
	}
	
	// Forecast = last_value + h * drift
	std::vector<double> forecast(horizon);
	for (int h = 0; h < horizon; ++h) {
		forecast[h] = last_value_ + static_cast<double>(h + 1) * drift_;
	}
	
	core::Forecast result;
	result.primary() = forecast;
	return result;
}

core::Forecast RandomWalkWithDrift::predictWithConfidence(int horizon, double confidence) {
	if (confidence <= 0.0 || confidence >= 1.0) {
		throw std::invalid_argument("Confidence level must be between 0 and 1");
	}
	
	auto forecast = predict(horizon);
	
	// Compute residual variance
	if (residuals_.empty() || residuals_.size() < 2) {
		return forecast;
	}
	
	double sum_sq = 0.0;
	for (size_t i = 1; i < residuals_.size(); ++i) {  // Skip first
		sum_sq += residuals_[i] * residuals_[i];
	}
	const double sigma = std::sqrt(sum_sq / static_cast<double>(residuals_.size() - 1));
	
	const double z = 1.96;  // Approximate 95% CI
	
	// Variance grows with horizon for random walk
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

