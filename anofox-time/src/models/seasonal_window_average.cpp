#include "anofox-time/models/seasonal_window_average.hpp"
#include "anofox-time/utils/logging.hpp"
#include <cmath>
#include <stdexcept>
#include <numeric>

namespace anofoxtime::models {

SeasonalWindowAverage::SeasonalWindowAverage(int seasonal_period, int window)
    : seasonal_period_(seasonal_period), window_(window) {
	if (seasonal_period_ < 1) {
		throw std::invalid_argument("Seasonal period must be >= 1");
	}
	if (window_ < 1) {
		throw std::invalid_argument("Window must be >= 1");
	}
}

double SeasonalWindowAverage::computeSeasonalAverage(size_t target_idx, int window_size) const {
	const size_t season_idx = target_idx % static_cast<size_t>(seasonal_period_);
	
	std::vector<double> seasonal_values;
	seasonal_values.reserve(window_size);
	
	// Collect last 'window_size' values from the same season
	for (int w = 0; w < window_size; ++w) {
		const int lookback = seasonal_period_ * (w + 1);
		if (target_idx >= static_cast<size_t>(lookback)) {
			const size_t idx = target_idx - static_cast<size_t>(lookback);
			if (idx < history_.size()) {
				seasonal_values.push_back(history_[idx]);
			}
		}
	}
	
	if (seasonal_values.empty()) {
		// Fallback: use the last available value from this season
		if (target_idx >= static_cast<size_t>(seasonal_period_)) {
			return history_[target_idx - static_cast<size_t>(seasonal_period_)];
		}
		return history_[target_idx];
	}
	
	// Return average
	const double sum = std::accumulate(seasonal_values.begin(), seasonal_values.end(), 0.0);
	return sum / static_cast<double>(seasonal_values.size());
}

void SeasonalWindowAverage::computeFittedValues() {
	const size_t n = history_.size();
	fitted_.resize(n);
	residuals_.resize(n);
	
	// First season has no seasonal forecast
	for (int i = 0; i < seasonal_period_ && i < static_cast<int>(n); ++i) {
		fitted_[i] = history_[i];
		residuals_[i] = 0.0;
	}
	
	// For each subsequent value, compute seasonal window average
	for (size_t i = static_cast<size_t>(seasonal_period_); i < n; ++i) {
		fitted_[i] = computeSeasonalAverage(i, window_);
		residuals_[i] = history_[i] - fitted_[i];
	}
}

void SeasonalWindowAverage::fit(const core::TimeSeries& ts) {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("SeasonalWindowAverage currently supports univariate series only");
	}
	
	if (ts.size() == 0) {
		throw std::invalid_argument("Cannot fit SeasonalWindowAverage on empty time series");
	}
	
	history_ = ts.getValues();
	
	if (history_.size() < static_cast<size_t>(seasonal_period_)) {
		throw std::invalid_argument("Time series must have at least one full seasonal cycle");
	}
	
	computeFittedValues();
	
	is_fitted_ = true;
	
	ANOFOX_INFO("SeasonalWindowAverage model fitted with {} data points, seasonal_period = {}, window = {}", 
	            history_.size(), seasonal_period_, window_);
}

core::Forecast SeasonalWindowAverage::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("SeasonalWindowAverage::predict called before fit");
	}
	
	if (horizon <= 0) {
		return {};
	}
	
	std::vector<double> forecast(horizon);
	
	// For each forecast step, average last 'window_' values from same season
	for (int h = 0; h < horizon; ++h) {
		const size_t future_idx = history_.size() + static_cast<size_t>(h);
		const size_t season_idx = future_idx % static_cast<size_t>(seasonal_period_);
		
		std::vector<double> seasonal_values;
		seasonal_values.reserve(window_);
		
		// Collect last 'window_' values from this season
		for (int w = 0; w < window_; ++w) {
			const int lookback = seasonal_period_ * (w + 1);
			if (history_.size() >= static_cast<size_t>(lookback)) {
				const size_t idx = history_.size() - static_cast<size_t>(lookback) + season_idx;
				if (idx < history_.size()) {
					seasonal_values.push_back(history_[idx]);
				}
			}
		}
		
		// Average the collected values
		if (!seasonal_values.empty()) {
			const double sum = std::accumulate(seasonal_values.begin(), seasonal_values.end(), 0.0);
			forecast[h] = sum / static_cast<double>(seasonal_values.size());
		} else {
			// Fallback: use last value from this season
			const size_t last_season_idx = history_.size() - static_cast<size_t>(seasonal_period_) + season_idx;
			forecast[h] = history_[last_season_idx];
		}
	}
	
	core::Forecast result;
	result.primary() = forecast;
	return result;
}

core::Forecast SeasonalWindowAverage::predictWithConfidence(int horizon, double confidence) {
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
	
	auto& lower = forecast.lowerSeries();
	auto& upper = forecast.upperSeries();
	lower.resize(horizon);
	upper.resize(horizon);
	
	for (int h = 0; h < horizon; ++h) {
		const int seasons_ahead = (h / seasonal_period_) + 1;
		const double std_h = sigma * std::sqrt(static_cast<double>(seasons_ahead));
		lower[h] = forecast.primary()[h] - z * std_h;
		upper[h] = forecast.primary()[h] + z * std_h;
	}
	
	return forecast;
}

} // namespace anofoxtime::models

