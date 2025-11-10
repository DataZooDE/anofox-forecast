#include "anofox-time/models/mstl_forecaster.hpp"
#include "anofox-time/models/auto_ets.hpp"
#include "anofox-time/models/auto_arima.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <stdexcept>
#include <iostream>

namespace anofoxtime::models {

MSTLForecaster::MSTLForecaster(
	std::vector<int> seasonal_periods,
	TrendMethod trend_method,
	SeasonalMethod seasonal_method,
	DeseasonalizedForecastMethod deseasonalized_method,
	int mstl_iterations,
	bool robust
)
	: seasonal_periods_(std::move(seasonal_periods))
	, trend_method_(trend_method)
	, seasonal_method_(seasonal_method)
	, deseasonalized_method_(deseasonalized_method)
	, mstl_iterations_(std::max(1, mstl_iterations))
	, robust_(robust)
{
	if (seasonal_periods_.empty()) {
		throw std::invalid_argument("MSTL: seasonal_periods cannot be empty");
	}
	
	for (int period : seasonal_periods_) {
		if (period < 2) {
			throw std::invalid_argument("MSTL: all seasonal periods must be >= 2");
		}
	}
}

void MSTLForecaster::fit(const core::TimeSeries& ts) {
	history_ = ts.getValues();
	const int n = static_cast<int>(history_.size());
	
	if (n < 2 * (*std::min_element(seasonal_periods_.begin(), seasonal_periods_.end()))) {
		throw std::runtime_error("MSTL: Insufficient data for decomposition");
	}
	
	// Convert int periods to size_t for MSTLDecomposition
	std::vector<std::size_t> periods_sizet;
	periods_sizet.reserve(seasonal_periods_.size());
	for (int p : seasonal_periods_) {
		periods_sizet.push_back(static_cast<std::size_t>(p));
	}
	
	// Create and fit MSTL decomposition
	decomposition_ = std::make_unique<seasonality::MSTLDecomposition>(
		periods_sizet,
		static_cast<std::size_t>(mstl_iterations_),
		robust_
	);
	
	decomposition_->fit(ts);
	is_fitted_ = true;
}

core::Forecast MSTLForecaster::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("MSTL: Must call fit() before predict()");
	}
	
	if (horizon <= 0) {
		throw std::invalid_argument("MSTL: horizon must be positive");
	}
	
	// Forecast the deseasonalized series (trend + remainder) 
	// This matches statsforecast's approach: x_sa = trend + remainder
	std::vector<double> deseasonalized_forecast = forecastDeseasonalized(horizon);
	
	// Initialize forecast with deseasonalized component
	std::vector<double> forecast_values = deseasonalized_forecast;
	
	// Add all seasonal components
	const auto& components = decomposition_->components();
	for (size_t i = 0; i < seasonal_periods_.size(); ++i) {
		std::vector<double> seasonal_forecast;
		
		switch (seasonal_method_) {
			case SeasonalMethod::Cyclic:
				seasonal_forecast = projectSeasonalCyclic(
					components.seasonal[i],
					seasonal_periods_[i],
					horizon
				);
				break;
			case SeasonalMethod::AutoETSAdditive:
				seasonal_forecast = forecastSeasonalAutoETSAdditive(
					components.seasonal[i],
					seasonal_periods_[i],
					horizon
				);
				break;
			case SeasonalMethod::AutoETSMultiplicative:
				seasonal_forecast = forecastSeasonalAutoETSMultiplicative(
					components.seasonal[i],
					seasonal_periods_[i],
					horizon
				);
				break;
		}
		
		for (int h = 0; h < horizon; ++h) {
			forecast_values[h] += seasonal_forecast[h];
		}
	}
	
	// Create and return forecast
	core::Forecast forecast;
	forecast.primary() = std::move(forecast_values);
	
	return forecast;
}

std::vector<double> MSTLForecaster::forecastTrendLinear(int horizon) {
	const auto& trend = decomposition_->components().trend;
	const int n = static_cast<int>(trend.size());
	
	// Simple linear regression on trend
	double mean_x = (n - 1) / 2.0;
	double mean_y = std::accumulate(trend.begin(), trend.end(), 0.0) / n;
	
	double numerator = 0.0;
	double denominator = 0.0;
	for (int i = 0; i < n; ++i) {
		double x_diff = i - mean_x;
		numerator += x_diff * (trend[i] - mean_y);
		denominator += x_diff * x_diff;
	}
	
	double slope = (denominator > 1e-10) ? (numerator / denominator) : 0.0;
	double intercept = mean_y - slope * mean_x;
	
	// Extrapolate
	std::vector<double> forecast(horizon);
	for (int h = 0; h < horizon; ++h) {
		forecast[h] = intercept + slope * (n + h);
	}
	
	return forecast;
}

std::vector<double> MSTLForecaster::forecastTrendSES(int horizon) {
	const auto& trend = decomposition_->components().trend;
	const int n = static_cast<int>(trend.size());
	
	// Simple exponential smoothing with alpha=0.3
	double alpha = 0.3;
	double level = trend[0];
	
	for (int i = 1; i < n; ++i) {
		level = alpha * trend[i] + (1.0 - alpha) * level;
	}
	
	// Constant forecast
	return std::vector<double>(horizon, level);
}

std::vector<double> MSTLForecaster::forecastTrendHolt(int horizon) {
	const auto& trend = decomposition_->components().trend;
	const int n = static_cast<int>(trend.size());
	
	// Holt's linear trend method
	double alpha = 0.8;  // Level smoothing
	double beta = 0.2;   // Trend smoothing
	
	double level = trend[0];
	double trend_component = 0.0;
	
	if (n > 1) {
		trend_component = trend[1] - trend[0];
	}
	
	for (int i = 1; i < n; ++i) {
		double new_level = alpha * trend[i] + (1.0 - alpha) * (level + trend_component);
		double new_trend = beta * (new_level - level) + (1.0 - beta) * trend_component;
		
		level = new_level;
		trend_component = new_trend;
	}
	
	// Linear forecast
	std::vector<double> forecast(horizon);
	for (int h = 0; h < horizon; ++h) {
		forecast[h] = level + (h + 1) * trend_component;
	}
	
	return forecast;
}

std::vector<double> MSTLForecaster::forecastTrendNone(int horizon) {
	const auto& trend = decomposition_->components().trend;
	
	// Use last trend value
	double last_trend = trend.back();
	return std::vector<double>(horizon, last_trend);
}

std::vector<double> MSTLForecaster::projectSeasonalCyclic(
	const std::vector<double>& seasonal,
	int period,
	int horizon
) {
	const int n = static_cast<int>(seasonal.size());
	std::vector<double> forecast(horizon);
	
	// Project seasonality cyclically
	for (int h = 0; h < horizon; ++h) {
		// Get the corresponding seasonal index
		int seasonal_idx = (n + h) % period;
		
		// Use the last complete cycle
		int start_of_last_cycle = n - period;
		if (start_of_last_cycle < 0) {
			start_of_last_cycle = 0;
		}
		
		int idx = start_of_last_cycle + seasonal_idx;
		if (idx >= n) {
			idx = n - period + seasonal_idx;
		}
		if (idx < 0 || idx >= n) {
			idx = seasonal_idx % n;
		}
		
		forecast[h] = seasonal[idx];
	}
	
	return forecast;
}

std::vector<double> MSTLForecaster::forecastTrendAutoETSAdditive(int horizon) {
	const auto& trend = decomposition_->components().trend;
	const int n = static_cast<int>(trend.size());
	
	if (n < 2) {
		return std::vector<double>(horizon, trend.back());
	}
	
	// Create TimeSeries from trend component
	// Trend is non-seasonal, so use season_length = 1
	std::vector<core::TimeSeries::TimePoint> timestamps;
	timestamps.reserve(n);
	auto start = core::TimeSeries::TimePoint{};
	for (int i = 0; i < n; ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	core::TimeSeries trend_ts(timestamps, trend);
	
	// Use AutoETS with "ZAN" spec (additive trend, no season)
	// Z = automatic error, A = additive trend, N = no season
	AutoETS autoets(1, "ZAN");
	autoets.fit(trend_ts);
	
	auto forecast = autoets.predict(horizon);
	return forecast.primary();
}

std::vector<double> MSTLForecaster::forecastTrendAutoETSMultiplicative(int horizon) {
	const auto& trend = decomposition_->components().trend;
	const int n = static_cast<int>(trend.size());
	
	if (n < 2) {
		return std::vector<double>(horizon, trend.back());
	}
	
	// Create TimeSeries from trend component
	std::vector<core::TimeSeries::TimePoint> timestamps;
	timestamps.reserve(n);
	auto start = core::TimeSeries::TimePoint{};
	for (int i = 0; i < n; ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	core::TimeSeries trend_ts(timestamps, trend);
	
	// Use AutoETS with "ZMN" spec (multiplicative trend, no season)
	// Z = automatic error, M = multiplicative trend, N = no season
	AutoETS autoets(1, "ZMN");
	autoets.fit(trend_ts);
	
	auto forecast = autoets.predict(horizon);
	return forecast.primary();
}

std::vector<double> MSTLForecaster::forecastSeasonalAutoETSAdditive(
	const std::vector<double>& seasonal,
	int period,
	int horizon
) {
	const int n = static_cast<int>(seasonal.size());
	
	if (n < 2 * period) {
		// Fallback to cyclic projection if insufficient data
		return projectSeasonalCyclic(seasonal, period, horizon);
	}
	
	// Create TimeSeries from seasonal component
	std::vector<core::TimeSeries::TimePoint> timestamps;
	timestamps.reserve(n);
	auto start = core::TimeSeries::TimePoint{};
	for (int i = 0; i < n; ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	core::TimeSeries seasonal_ts(timestamps, seasonal);
	
	// Use AutoETS with "ZNA" spec (no trend, additive season)
	// Z = automatic error, N = no trend, A = additive season
	AutoETS autoets(period, "ZNA");
	autoets.fit(seasonal_ts);
	
	auto forecast = autoets.predict(horizon);
	return forecast.primary();
}

std::vector<double> MSTLForecaster::forecastSeasonalAutoETSMultiplicative(
	const std::vector<double>& seasonal,
	int period,
	int horizon
) {
	const int n = static_cast<int>(seasonal.size());
	
	if (n < 2 * period) {
		// Fallback to cyclic projection if insufficient data
		return projectSeasonalCyclic(seasonal, period, horizon);
	}
	
	// Check for non-positive values (multiplicative seasonality requires positive data)
	bool has_non_positive = false;
	for (double val : seasonal) {
		if (val <= 0.0) {
			has_non_positive = true;
			break;
		}
	}
	
	if (has_non_positive) {
		// Fallback to additive if we have non-positive values
		return forecastSeasonalAutoETSAdditive(seasonal, period, horizon);
	}
	
	// Create TimeSeries from seasonal component
	std::vector<core::TimeSeries::TimePoint> timestamps;
	timestamps.reserve(n);
	auto start = core::TimeSeries::TimePoint{};
	for (int i = 0; i < n; ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	core::TimeSeries seasonal_ts(timestamps, seasonal);
	
	// Use AutoETS with "ZNM" spec (no trend, multiplicative season)
	// Z = automatic error, N = no trend, M = multiplicative season
	AutoETS autoets(period, "ZNM");
	autoets.fit(seasonal_ts);
	
	auto forecast = autoets.predict(horizon);
	return forecast.primary();
}

std::vector<double> MSTLForecaster::forecastDeseasonalized(int horizon) {
	// This method forecasts the deseasonalized series (trend + remainder)
	const auto& trend = decomposition_->components().trend;
	const auto& remainder = decomposition_->components().remainder;
	const int n = static_cast<int>(trend.size());
	
	if (n < 2) {
		// Fallback: if insufficient data, just use last trend value
		return std::vector<double>(horizon, trend.back() + (remainder.empty() ? 0.0 : remainder.back()));
	}
	
	// Combine trend and remainder (seasonally adjusted series)
	std::vector<double> x_sa(n);
	for (int i = 0; i < n; ++i) {
		x_sa[i] = trend[i] + remainder[i];
	}
	
	// Select forecasting method based on configuration
	switch (deseasonalized_method_) {
		case DeseasonalizedForecastMethod::ExponentialSmoothing: {
			// Fast simple exponential smoothing (default)
			double alpha = 0.3;
			double level = x_sa[0];
			
			for (int i = 1; i < n; ++i) {
				level = alpha * x_sa[i] + (1.0 - alpha) * level;
			}
			
			// Constant forecast (SES doesn't have trend)
			return std::vector<double>(horizon, level);
		}
		
		case DeseasonalizedForecastMethod::Linear: {
			// Linear regression extrapolation
			double mean_x = (n - 1) / 2.0;
			double mean_y = std::accumulate(x_sa.begin(), x_sa.end(), 0.0) / n;
			
			double numerator = 0.0;
			double denominator = 0.0;
			for (int i = 0; i < n; ++i) {
				double x_diff = i - mean_x;
				numerator += x_diff * (x_sa[i] - mean_y);
				denominator += x_diff * x_diff;
			}
			
			double slope = (denominator > 1e-10) ? (numerator / denominator) : 0.0;
			double intercept = mean_y - slope * mean_x;
			
			std::vector<double> forecast(horizon);
			for (int h = 0; h < horizon; ++h) {
				forecast[h] = intercept + slope * (n + h);
			}
			
			return forecast;
		}
		
		case DeseasonalizedForecastMethod::AutoETS: {
			// Full AutoETS (slowest but most accurate)
			std::vector<core::TimeSeries::TimePoint> timestamps;
			timestamps.reserve(n);
			auto start = core::TimeSeries::TimePoint{};
			for (int i = 0; i < n; ++i) {
				timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
			}
			
			core::TimeSeries x_sa_ts(timestamps, x_sa);
			
			try {
				AutoETS autoets(1, "ZZN");  // season_length=1, non-seasonal
				autoets.fit(x_sa_ts);
				auto forecast = autoets.predict(horizon);
				return forecast.primary();
			} catch (const std::exception& e) {
				// Fallback to linear extrapolation if AutoETS fails
				double mean_x = (n - 1) / 2.0;
				double mean_y = std::accumulate(x_sa.begin(), x_sa.end(), 0.0) / n;
				
				double numerator = 0.0;
				double denominator = 0.0;
				for (int i = 0; i < n; ++i) {
					double x_diff = i - mean_x;
					numerator += x_diff * (x_sa[i] - mean_y);
					denominator += x_diff * x_diff;
				}
				
				double slope = (denominator > 1e-10) ? (numerator / denominator) : 0.0;
				double intercept = mean_y - slope * mean_x;
				
				std::vector<double> forecast(horizon);
				for (int h = 0; h < horizon; ++h) {
					forecast[h] = intercept + slope * (n + h);
				}
				
				return forecast;
			}
		}
		
		default:
			// Fallback to exponential smoothing
			double alpha = 0.3;
			double level = x_sa[0];
			for (int i = 1; i < n; ++i) {
				level = alpha * x_sa[i] + (1.0 - alpha) * level;
			}
			return std::vector<double>(horizon, level);
	}
}

} // namespace anofoxtime::models

