#include "anofox-time/models/seasonal_naive.hpp"
#include "anofox-time/utils/logging.hpp"
#include <cmath>
#include <stdexcept>
#include <numeric>

namespace anofoxtime::models {

SeasonalNaive::SeasonalNaive(int seasonal_period) : seasonal_period_(seasonal_period) {
	if (seasonal_period_ < 1) {
		throw std::invalid_argument("Seasonal period must be >= 1");
	}
}

void SeasonalNaive::computeFittedValues() {
	const size_t n = history_.size();
	fitted_.resize(n);
	residuals_.resize(n);
	
	// First season has no seasonal forecast
	for (int i = 0; i < seasonal_period_ && i < static_cast<int>(n); ++i) {
		fitted_[i] = history_[i];
		residuals_[i] = 0.0;
	}
	
	// For each subsequent value, use value from same season last cycle
	for (size_t i = static_cast<size_t>(seasonal_period_); i < n; ++i) {
		fitted_[i] = history_[i - static_cast<size_t>(seasonal_period_)];
		residuals_[i] = history_[i] - fitted_[i];
	}
}

void SeasonalNaive::fit(const core::TimeSeries& ts) {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("SeasonalNaive currently supports univariate series only");
	}
	
	if (ts.size() == 0) {
		throw std::invalid_argument("Cannot fit SeasonalNaive on empty time series");
	}
	
	history_ = ts.getValues();
	
	if (history_.size() < static_cast<size_t>(seasonal_period_)) {
		throw std::invalid_argument("Time series must have at least one full seasonal cycle");
	}
	
	computeFittedValues();
	
	is_fitted_ = true;
	
	ANOFOX_INFO("SeasonalNaive model fitted with {} data points, seasonal_period = {}", 
	            history_.size(), seasonal_period_);
}

core::Forecast SeasonalNaive::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("SeasonalNaive::predict called before fit");
	}
	
	if (horizon <= 0) {
		return {};
	}
	
	std::vector<double> forecast(horizon);
	
	// For each forecast step, use the value from the same season in the last cycle
	for (int h = 0; h < horizon; ++h) {
		// Determine which season this forecast belongs to
		const size_t total_idx = history_.size() + static_cast<size_t>(h);
		const size_t season_idx = total_idx % static_cast<size_t>(seasonal_period_);
		
		// Get the most recent value from this season
		// This is seasonal_period steps back from where we'd be
		const size_t lookback_idx = history_.size() - static_cast<size_t>(seasonal_period_) + season_idx;
		
		forecast[h] = history_[lookback_idx];
	}
	
	core::Forecast result;
	result.primary() = forecast;
	return result;
}

core::Forecast SeasonalNaive::predictWithConfidence(int horizon, double confidence) {
	if (confidence <= 0.0 || confidence >= 1.0) {
		throw std::invalid_argument("Confidence level must be between 0 and 1");
	}
	
	auto forecast = predict(horizon);
	
	// Compute residual variance
	if (residuals_.empty() || residuals_.size() <= static_cast<size_t>(seasonal_period_)) {
		return forecast;
	}
	
	double sum_sq = 0.0;
	size_t count = 0;
	for (size_t i = static_cast<size_t>(seasonal_period_); i < residuals_.size(); ++i) {
		sum_sq += residuals_[i] * residuals_[i];
		count++;
	}
	
	if (count == 0) {
		return forecast;
	}
	
	const double sigma = std::sqrt(sum_sq / static_cast<double>(count));
	const double z = 1.96;  // Approximate 95% CI
	
	// For seasonal naive, variance depends on how many seasons ahead
	auto& lower = forecast.lowerSeries();
	auto& upper = forecast.upperSeries();
	lower.resize(horizon);
	upper.resize(horizon);
	
	for (int h = 0; h < horizon; ++h) {
		// Number of full seasons ahead
		const int seasons_ahead = (h / seasonal_period_) + 1;
		const double std_h = sigma * std::sqrt(static_cast<double>(seasons_ahead));
		lower[h] = forecast.primary()[h] - z * std_h;
		upper[h] = forecast.primary()[h] + z * std_h;
	}
	
	return forecast;
}

} // namespace anofoxtime::models

