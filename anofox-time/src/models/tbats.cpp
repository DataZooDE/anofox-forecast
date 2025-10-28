#include "anofox-time/models/tbats.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <stdexcept>

namespace anofoxtime::models {

namespace {
	constexpr double PI = 3.14159265358979323846;
	constexpr double EPSILON = 1e-10;
}

TBATS::TBATS(const Config& config)
	: config_(config)
{
	if (config_.seasonal_periods.empty()) {
		throw std::invalid_argument("TBATS: seasonal_periods cannot be empty");
	}
	
	for (int period : config_.seasonal_periods) {
		if (period < 2) {
			throw std::invalid_argument("TBATS: all seasonal periods must be >= 2");
		}
	}
	
	if (config_.ar_order < 0 || config_.ar_order > 5) {
		throw std::invalid_argument("TBATS: ar_order must be in [0, 5]");
	}
	
	if (config_.ma_order < 0 || config_.ma_order > 5) {
		throw std::invalid_argument("TBATS: ma_order must be in [0, 5]");
	}
	
	if (config_.use_damped_trend && (config_.damping_param <= 0.0 || config_.damping_param > 1.0)) {
		throw std::invalid_argument("TBATS: damping_param must be in (0, 1]");
	}
}

void TBATS::fit(const core::TimeSeries& ts) {
	history_ = ts.getValues();
	const int n = static_cast<int>(history_.size());
	
	if (n < 4) {
		throw std::runtime_error("TBATS requires at least 4 data points");
	}
	
	// Apply Box-Cox transformation if enabled
	if (config_.use_box_cox) {
		transformed_history_ = applyBoxCox(history_);
	} else {
		transformed_history_ = history_;
	}
	
	// Auto-select Fourier K if not provided
	if (config_.fourier_k.empty()) {
		for (int period : config_.seasonal_periods) {
			int k = selectOptimalK(period);
			config_.fourier_k.push_back(k);
		}
	}
	
	// Fit state-space model
	fitStateSpace(transformed_history_);
	
	// Compute fitted values and residuals
	computeFittedValues();
	
	// Compute AIC
	int num_params = 1;  // Level
	if (config_.use_trend) num_params += 1;
	for (size_t i = 0; i < config_.seasonal_periods.size(); ++i) {
		num_params += 2 * config_.fourier_k[i];  // Sin/cos pairs
	}
	num_params += config_.ar_order + config_.ma_order;
	
	computeAIC(residuals_, num_params);
	
	is_fitted_ = true;
}

std::vector<double> TBATS::applyBoxCox(const std::vector<double>& data) {
	std::vector<double> transformed(data.size());
	
	for (size_t i = 0; i < data.size(); ++i) {
		transformed[i] = boxCoxTransform(data[i]);
	}
	
	return transformed;
}

std::vector<double> TBATS::inverseBoxCox(const std::vector<double>& data) {
	std::vector<double> original(data.size());
	
	for (size_t i = 0; i < data.size(); ++i) {
		original[i] = inverseBoxCoxTransform(data[i]);
	}
	
	return original;
}

double TBATS::boxCoxTransform(double value) {
	if (!config_.use_box_cox) {
		return value;
	}
	
	double lambda = config_.box_cox_lambda;
	
	if (std::abs(lambda) < EPSILON) {
		// Log transformation
		if (value <= 0.0) {
			throw std::runtime_error("TBATS: Box-Cox with lambda=0 requires positive values");
		}
		return std::log(value);
	} else {
		// Power transformation
		if (value <= 0.0 && lambda < 1.0) {
			throw std::runtime_error("TBATS: Box-Cox requires positive values for lambda < 1");
		}
		return (std::pow(value, lambda) - 1.0) / lambda;
	}
}

double TBATS::inverseBoxCoxTransform(double value) {
	if (!config_.use_box_cox) {
		return value;
	}
	
	double lambda = config_.box_cox_lambda;
	
	if (std::abs(lambda) < EPSILON) {
		// Inverse log
		return std::exp(value);
	} else {
		// Inverse power
		double result = lambda * value + 1.0;
		if (result <= 0.0) {
			return EPSILON;  // Avoid negative values
		}
		return std::pow(result, 1.0 / lambda);
	}
}

int TBATS::selectOptimalK(int period, int max_k) {
	// Select K based on period size
	// Following De Livera et al.: K = min(period/2, max_k)
	int k = std::min(period / 2, max_k);
	return std::max(1, k);
}

void TBATS::initializeStates(const std::vector<double>& data) {
	const int n = static_cast<int>(data.size());
	
	// Initialize level as mean of first few observations
	int init_window = std::min(10, n);
	level_state_ = 0.0;
	for (int i = 0; i < init_window; ++i) {
		level_state_ += data[i];
	}
	level_state_ /= init_window;
	
	// Initialize trend
	if (config_.use_trend && n > 1) {
		trend_state_ = data[1] - data[0];
	} else {
		trend_state_ = 0.0;
	}
	
	// Initialize seasonal states (Fourier coefficients)
	for (size_t idx = 0; idx < config_.seasonal_periods.size(); ++idx) {
		int period = config_.seasonal_periods[idx];
		int K = config_.fourier_k[idx];
		
		// Initialize K sin/cos pairs
		std::vector<std::pair<double, double>> fourier_states(K, {0.0, 0.0});
		seasonal_states_[period] = fourier_states;
	}
	
	// Initialize ARMA states
	ar_states_.assign(config_.ar_order, 0.0);
	ma_states_.assign(config_.ma_order, 0.0);
	
	// Initialize smoothing parameters if not set
	alpha_estimated_ = (config_.alpha > 0.0) ? config_.alpha : 0.1;
	beta_estimated_ = (config_.beta > 0.0) ? config_.beta : 0.01;
	
	gamma_estimated_.clear();
	for (size_t i = 0; i < config_.seasonal_periods.size(); ++i) {
		if (i < config_.gamma.size() && config_.gamma[i] > 0.0) {
			gamma_estimated_.push_back(config_.gamma[i]);
		} else {
			gamma_estimated_.push_back(0.05);  // Default
		}
	}
}

void TBATS::fitStateSpace(const std::vector<double>& data) {
	const int n = static_cast<int>(data.size());
	
	// Initialize states
	initializeStates(data);
	
	// Allocate storage
	fitted_.resize(n);
	residuals_.resize(n);
	
	// Simple state-space update (simplified Kalman filter)
	for (int t = 0; t < n; ++t) {
		double observation = data[t];
		double fitted_value = 0.0;
		double error = 0.0;
		
		updateStates(observation, fitted_value, error);
		
		fitted_[t] = fitted_value;
		residuals_[t] = error;
	}
}

void TBATS::updateStates(double observation, double& fitted_value, double& error) {
	// Compute fitted value from current states
	fitted_value = level_state_;
	
	if (config_.use_trend) {
		if (config_.use_damped_trend) {
			fitted_value += config_.damping_param * trend_state_;
		} else {
			fitted_value += trend_state_;
		}
	}
	
	// Add seasonal components (Fourier)
	for (size_t idx = 0; idx < config_.seasonal_periods.size(); ++idx) {
		int period = config_.seasonal_periods[idx];
		const auto& states = seasonal_states_[period];
		
		for (const auto& [sin_state, cos_state] : states) {
			fitted_value += sin_state + cos_state;
		}
	}
	
	// Compute innovation error
	error = observation - fitted_value;
	
	// Update level
	double new_level = level_state_;
	if (config_.use_trend) {
		new_level += config_.use_damped_trend ? config_.damping_param * trend_state_ : trend_state_;
	}
	new_level += alpha_estimated_ * error;
	
	// Update trend
	double new_trend = trend_state_;
	if (config_.use_trend) {
		new_trend = (config_.use_damped_trend ? config_.damping_param : 1.0) * trend_state_;
		new_trend += beta_estimated_ * error;
	}
	
	// Update seasonal states (simplified - in full TBATS this uses transition matrices)
	for (size_t idx = 0; idx < config_.seasonal_periods.size(); ++idx) {
		int period = config_.seasonal_periods[idx];
		auto& states = seasonal_states_[period];
		double gamma = gamma_estimated_[idx];
		
		// Simple update: adjust all Fourier pairs slightly
		for (auto& [sin_state, cos_state] : states) {
			sin_state += gamma * error / (2.0 * states.size());
			cos_state += gamma * error / (2.0 * states.size());
		}
	}
	
	// Update states
	level_state_ = new_level;
	trend_state_ = new_trend;
	
	// Handle ARMA errors (simplified)
	if (config_.ar_order > 0 || config_.ma_order > 0) {
		// Shift AR states
		for (int i = config_.ar_order - 1; i > 0; --i) {
			ar_states_[i] = ar_states_[i - 1];
		}
		if (config_.ar_order > 0) {
			ar_states_[0] = error;
		}
		
		// Shift MA states
		for (int i = config_.ma_order - 1; i > 0; --i) {
			ma_states_[i] = ma_states_[i - 1];
		}
		if (config_.ma_order > 0) {
			ma_states_[0] = error;
		}
	}
}

core::Forecast TBATS::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("TBATS: Must call fit() before predict()");
	}
	
	if (horizon <= 0) {
		throw std::invalid_argument("TBATS: horizon must be positive");
	}
	
	// Forecast in transformed space
	auto forecast_transformed = forecastStateSpace(horizon);
	
	// Apply inverse Box-Cox if needed
	std::vector<double> forecast_values;
	if (config_.use_box_cox) {
		forecast_values = inverseBoxCox(forecast_transformed);
	} else {
		forecast_values = forecast_transformed;
	}
	
	// Create and return forecast
	core::Forecast forecast;
	forecast.primary() = std::move(forecast_values);
	
	return forecast;
}

std::vector<double> TBATS::forecastStateSpace(int horizon) {
	std::vector<double> forecast(horizon);
	
	// Current states
	double level = level_state_;
	double trend = trend_state_;
	auto seasonal_states_copy = seasonal_states_;
	
	const int n = static_cast<int>(transformed_history_.size());
	
	for (int h = 0; h < horizon; ++h) {
		// Forecast = level + trend + seasonality
		forecast[h] = level;
		
		if (config_.use_trend) {
			if (config_.use_damped_trend) {
				// Damped trend accumulation
				double damping_power = 0.0;
				for (int j = 0; j <= h; ++j) {
					damping_power += std::pow(config_.damping_param, j);
				}
				forecast[h] += damping_power * trend;
			} else {
				forecast[h] += (h + 1) * trend;
			}
		}
		
		// Add seasonal components (Fourier projection)
		for (size_t idx = 0; idx < config_.seasonal_periods.size(); ++idx) {
			int period = config_.seasonal_periods[idx];
			int K = config_.fourier_k[idx];
			const auto& states = seasonal_states_copy[period];
			
			int t = n + h;
			double seasonal_value = 0.0;
			
			for (int k = 0; k < K; ++k) {
				double angle = 2.0 * PI * (k + 1) * t / period;
				// Use current Fourier states
				seasonal_value += states[k].first * std::sin(angle);
				seasonal_value += states[k].second * std::cos(angle);
			}
			
			forecast[h] += seasonal_value;
		}
		
		// Update states for next step (propagate trend)
		if (config_.use_trend) {
			if (config_.use_damped_trend) {
				level += config_.damping_param * trend;
				trend *= config_.damping_param;
			} else {
				level += trend;
			}
		}
	}
	
	return forecast;
}

void TBATS::computeFittedValues() {
	// Fitted values already computed in fitStateSpace
	// If using Box-Cox, transform fitted values back
	if (config_.use_box_cox) {
		std::vector<double> fitted_transformed = fitted_;
		fitted_ = inverseBoxCox(fitted_transformed);
		
		// Recompute residuals in original space
		for (size_t i = 0; i < history_.size(); ++i) {
			residuals_[i] = history_[i] - fitted_[i];
		}
	}
}

void TBATS::computeAIC(const std::vector<double>& residuals, int num_params) {
	const int n = static_cast<int>(residuals.size());
	
	// Compute RSS
	double rss = 0.0;
	for (double r : residuals) {
		rss += r * r;
	}
	
	if (rss <= 0.0 || n <= num_params) {
		aic_ = std::numeric_limits<double>::infinity();
		return;
	}
	
	// AIC = 2k + n*log(RSS/n)
	aic_ = 2.0 * num_params + n * std::log(rss / n);
}

} // namespace anofoxtime::models

