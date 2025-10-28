#include "anofox-time/models/seasonal_es.hpp"
#include "anofox-time/utils/logging.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <stdexcept>

namespace anofoxtime::models {

namespace {
	constexpr double kEpsilon = 1e-10;
}

SeasonalExponentialSmoothing::SeasonalExponentialSmoothing(int seasonal_period, double alpha, double gamma)
    : seasonal_period_(seasonal_period), alpha_(alpha), gamma_(gamma), level_(0.0) {
	if (seasonal_period_ < 2) {
		throw std::invalid_argument("Seasonal period must be >= 2");
	}
	if (alpha_ < 0.0 || alpha_ > 1.0) {
		throw std::invalid_argument("Alpha must be in [0, 1]");
	}
	if (gamma_ < 0.0 || gamma_ > 1.0) {
		throw std::invalid_argument("Gamma must be in [0, 1]");
	}
}

void SeasonalExponentialSmoothing::initializeSeasonalIndices(const std::vector<double>& data) {
	const std::size_t n = data.size();
	seasonal_indices_.resize(seasonal_period_, 1.0);
	
	if (n < 2 * static_cast<std::size_t>(seasonal_period_)) {
		// Insufficient data for proper initialization, use simple average
		for (int s = 0; s < seasonal_period_; ++s) {
			double sum = 0.0;
			int count = 0;
			for (std::size_t i = s; i < n; i += seasonal_period_) {
				sum += data[i];
				count++;
			}
			if (count > 0) {
				seasonal_indices_[s] = sum / static_cast<double>(count);
			}
		}
		
		// Normalize to average 1.0
		double avg = std::accumulate(seasonal_indices_.begin(), seasonal_indices_.end(), 0.0) / 
		             static_cast<double>(seasonal_period_);
		if (avg > kEpsilon) {
			for (double& idx : seasonal_indices_) {
				idx /= avg;
			}
		}
		return;
	}
	
	// Use first two complete cycles for initialization
	std::vector<double> cycle_avgs(seasonal_period_, 0.0);
	for (int s = 0; s < seasonal_period_; ++s) {
		double sum = 0.0;
		sum += data[s];  // First cycle
		sum += data[s + seasonal_period_];  // Second cycle
		cycle_avgs[s] = sum / 2.0;
	}
	
	// Normalize
	double total = std::accumulate(cycle_avgs.begin(), cycle_avgs.end(), 0.0);
	double avg = total / static_cast<double>(seasonal_period_);
	
	if (avg > kEpsilon) {
		for (int s = 0; s < seasonal_period_; ++s) {
			seasonal_indices_[s] = cycle_avgs[s] / avg;
		}
	}
	
	// Initialize level as average of first season (deseasonalized)
	double sum = 0.0;
	for (int s = 0; s < seasonal_period_; ++s) {
		if (seasonal_indices_[s] > kEpsilon) {
			sum += data[s] / seasonal_indices_[s];
		}
	}
	level_ = sum / static_cast<double>(seasonal_period_);
}

void SeasonalExponentialSmoothing::computeFittedValues() {
	const std::size_t n = history_.size();
	fitted_.resize(n);
	residuals_.resize(n);
	
	// Reinitialize for fitted value computation
	double level = level_;
	std::vector<double> seasonal = seasonal_indices_;
	
	// First season: use initial seasonal indices
	for (int i = 0; i < seasonal_period_ && i < static_cast<int>(n); ++i) {
		fitted_[i] = level * seasonal[i];
		residuals_[i] = history_[i] - fitted_[i];
	}
	
	// Subsequent observations
	for (std::size_t t = seasonal_period_; t < n; ++t) {
		const std::size_t s_idx = t % static_cast<std::size_t>(seasonal_period_);
		
		// One-step-ahead forecast
		fitted_[t] = level * seasonal[s_idx];
		residuals_[t] = history_[t] - fitted_[t];
		
		// Update level and seasonal
		const double prev_level = level;
		if (seasonal[s_idx] > kEpsilon) {
			level = alpha_ * (history_[t] / seasonal[s_idx]) + (1.0 - alpha_) * level;
		}
		if (level > kEpsilon) {
			seasonal[s_idx] = gamma_ * (history_[t] / level) + (1.0 - gamma_) * seasonal[s_idx];
		}
	}
}

void SeasonalExponentialSmoothing::fit(const core::TimeSeries& ts) {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("SeasonalExponentialSmoothing currently supports univariate series only");
	}
	
	const auto data = ts.getValues();
	
	if (data.empty()) {
		throw std::invalid_argument("Cannot fit on empty time series");
	}
	
	if (data.size() < static_cast<std::size_t>(seasonal_period_)) {
		throw std::invalid_argument("Time series must have at least one full seasonal cycle");
	}
	
	history_ = data;
	
	// Initialize seasonal indices and level
	initializeSeasonalIndices(history_);
	
	// Apply smoothing
	for (std::size_t t = seasonal_period_; t < history_.size(); ++t) {
		const std::size_t s_idx = t % static_cast<std::size_t>(seasonal_period_);
		
		// Update level
		if (seasonal_indices_[s_idx] > kEpsilon) {
			level_ = alpha_ * (history_[t] / seasonal_indices_[s_idx]) + (1.0 - alpha_) * level_;
		}
		
		// Update seasonal index
		if (level_ > kEpsilon) {
			seasonal_indices_[s_idx] = gamma_ * (history_[t] / level_) + (1.0 - gamma_) * seasonal_indices_[s_idx];
		}
	}
	
	// Compute fitted values and residuals
	computeFittedValues();
	
	is_fitted_ = true;
	
	ANOFOX_INFO("SeasonalES model fitted with alpha={:.3f}, gamma={:.3f}, seasonal_period={}", 
	            alpha_, gamma_, seasonal_period_);
}

core::Forecast SeasonalExponentialSmoothing::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("SeasonalES::predict called before fit");
	}
	
	if (horizon <= 0) {
		return {};
	}
	
	std::vector<double> forecast(horizon);
	
	// Forecast: level × seasonal_index
	for (int h = 0; h < horizon; ++h) {
		const std::size_t s_idx = (history_.size() + h) % static_cast<std::size_t>(seasonal_period_);
		forecast[h] = level_ * seasonal_indices_[s_idx];
	}
	
	core::Forecast result;
	result.primary() = forecast;
	return result;
}

core::Forecast SeasonalExponentialSmoothing::predictWithConfidence(int horizon, double confidence) {
	if (confidence <= 0.0 || confidence >= 1.0) {
		throw std::invalid_argument("Confidence level must be between 0 and 1");
	}
	
	auto forecast = predict(horizon);
	
	// Compute residual variance
	if (residuals_.empty()) {
		return forecast;
	}
	
	double sum_sq = 0.0;
	for (std::size_t i = seasonal_period_; i < residuals_.size(); ++i) {
		sum_sq += residuals_[i] * residuals_[i];
	}
	const double sigma = std::sqrt(sum_sq / static_cast<double>(residuals_.size() - seasonal_period_));
	
	const double z = 1.96;  // 95% CI
	
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

