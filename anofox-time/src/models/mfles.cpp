#include "anofox-time/models/mfles.hpp"
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <algorithm>

namespace anofoxtime::models {

namespace {
	constexpr double PI = 3.14159265358979323846;
	constexpr int MAX_FOURIER_TERMS = 10;  // Maximum K value
}

MFLES::MFLES(std::vector<int> seasonal_periods, 
             int n_iterations,
             double lr_trend,
             double lr_season,
             double lr_level)
	: seasonal_periods_(std::move(seasonal_periods))
	, n_iterations_(n_iterations)
	, lr_trend_(lr_trend)
	, lr_season_(lr_season)
	, lr_level_(lr_level)
{
	if (n_iterations_ < 1) {
		throw std::invalid_argument("MFLES: n_iterations must be at least 1");
	}
	if (lr_trend_ < 0.0 || lr_trend_ > 1.0) {
		throw std::invalid_argument("MFLES: lr_trend must be in [0, 1]");
	}
	if (lr_season_ < 0.0 || lr_season_ > 1.0) {
		throw std::invalid_argument("MFLES: lr_season must be in [0, 1]");
	}
	if (lr_level_ < 0.0 || lr_level_ > 1.0) {
		throw std::invalid_argument("MFLES: lr_level must be in [0, 1]");
	}
	
	// Validate seasonal periods
	for (int period : seasonal_periods_) {
		if (period < 1) {
			throw std::invalid_argument("MFLES: seasonal periods must be >= 1");
		}
	}
}

void MFLES::fit(const core::TimeSeries& ts) {
	history_ = ts.getValues();
	const int n = static_cast<int>(history_.size());
	
	if (n < 3) {
		throw std::runtime_error("MFLES requires at least 3 data points");
	}
	
	// Initialize components
	trend_component_ = std::vector<double>(n, 0.0);
	level_component_ = std::vector<double>(n, 0.0);
	for (int period : seasonal_periods_) {
		seasonal_components_[period] = std::vector<double>(n, 0.0);
	}
	
	// Initialize residuals with data
	std::vector<double> residuals = history_;
	
	// Initialize accumulated parameters
	double accumulated_slope = 0.0;
	double accumulated_intercept = 0.0;
	std::map<int, FourierCoeffs> accumulated_fourier;
	double accumulated_level = 0.0;
	
	// Gradient boosting iterations
	// Learning rates control how much of each fitted component to add per iteration
	for (int iter = 0; iter < n_iterations_; ++iter) {
		// 1. Fit linear trend on residuals
		if (lr_trend_ > 0.0) {
			auto trend = fitLinearTrend(residuals);
			// Accumulate trend parameters
			accumulated_slope += lr_trend_ * trend_slope_;
			accumulated_intercept += lr_trend_ * trend_intercept_;
			
			for (int i = 0; i < n; ++i) {
				trend_component_[i] += lr_trend_ * trend[i];
				residuals[i] -= lr_trend_ * trend[i];
			}
		}
		
		// 2. Fit Fourier seasonality for each period
		if (lr_season_ > 0.0) {
			for (int period : seasonal_periods_) {
				if (n >= 2 * period) {  // Need at least 2 cycles
					auto seasonal = fitFourierSeason(residuals, period);
					
					// Accumulate Fourier coefficients
					if (accumulated_fourier.find(period) == accumulated_fourier.end()) {
						accumulated_fourier[period] = fourier_coeffs_[period];
						// Scale by learning rate
						for (auto& c : accumulated_fourier[period].sin_coeffs) c *= lr_season_;
						for (auto& c : accumulated_fourier[period].cos_coeffs) c *= lr_season_;
					} else {
						// Add current coefficients (scaled by LR)
						for (size_t k = 0; k < fourier_coeffs_[period].sin_coeffs.size(); ++k) {
							accumulated_fourier[period].sin_coeffs[k] += lr_season_ * fourier_coeffs_[period].sin_coeffs[k];
							accumulated_fourier[period].cos_coeffs[k] += lr_season_ * fourier_coeffs_[period].cos_coeffs[k];
						}
					}
					
					for (int i = 0; i < n; ++i) {
						seasonal_components_[period][i] += lr_season_ * seasonal[i];
						residuals[i] -= lr_season_ * seasonal[i];
					}
				}
			}
		}
		
		// 3. Fit ES level on residuals
		if (lr_level_ > 0.0) {
			auto level = fitESLevel(residuals);
			accumulated_level += lr_level_ * es_level_;
			
			for (int i = 0; i < n; ++i) {
				level_component_[i] += lr_level_ * level[i];
				residuals[i] -= lr_level_ * level[i];
			}
		}
		
		// Early stopping: if residuals converge
		double residual_std = 0.0;
		for (const auto& r : residuals) {
			residual_std += r * r;
		}
		residual_std = std::sqrt(residual_std / n);
		
		double data_range = *std::max_element(history_.begin(), history_.end()) - 
		                   *std::min_element(history_.begin(), history_.end());
		if (residual_std < 0.01 * data_range && iter >= 5) {
			break;  // Converged
		}
	}
	
	// Store accumulated parameters for forecasting
	trend_slope_ = accumulated_slope;
	trend_intercept_ = accumulated_intercept;
	fourier_coeffs_ = accumulated_fourier;
	es_level_ = accumulated_level;
	
	// Compute fitted values and residuals
	computeFittedValues();
	
	is_fitted_ = true;
}

std::vector<double> MFLES::fitLinearTrend(const std::vector<double>& data) {
	const int n = static_cast<int>(data.size());
	
	// Compute mean of x (time indices) and y (data)
	double mean_x = (n - 1) / 2.0;  // 0, 1, 2, ..., n-1
	double mean_y = std::accumulate(data.begin(), data.end(), 0.0) / n;
	
	// Compute slope
	double numerator = 0.0;
	double denominator = 0.0;
	for (int i = 0; i < n; ++i) {
		double x_diff = i - mean_x;
		numerator += x_diff * (data[i] - mean_y);
		denominator += x_diff * x_diff;
	}
	
	double slope = (denominator > 1e-10) ? (numerator / denominator) : 0.0;
	double intercept = mean_y - slope * mean_x;
	
	// Store for forecasting
	trend_slope_ = slope;
	trend_intercept_ = intercept;
	
	// Generate trend line
	std::vector<double> trend(n);
	for (int i = 0; i < n; ++i) {
		trend[i] = intercept + slope * i;
	}
	
	return trend;
}

std::vector<double> MFLES::fitFourierSeason(const std::vector<double>& data, int period) {
	const int n = static_cast<int>(data.size());
	const int K = optimalK(period);
	
	if (K == 0 || n < period) {
		return std::vector<double>(n, 0.0);
	}
	
	// Build design matrix: [sin(2πt/s), cos(2πt/s), sin(4πt/s), cos(4πt/s), ...]
	// For k = 1 to K
	std::vector<std::vector<double>> X(n, std::vector<double>(2 * K));
	
	for (int i = 0; i < n; ++i) {
		for (int k = 1; k <= K; ++k) {
			double angle = 2.0 * PI * k * i / period;
			X[i][2 * (k - 1)] = std::sin(angle);      // sin term
			X[i][2 * (k - 1) + 1] = std::cos(angle);  // cos term
		}
	}
	
	// Solve least squares: X^T X β = X^T y
	// Using normal equations (simplified for this case)
	std::vector<double> coeffs(2 * K, 0.0);
	
	for (int j = 0; j < 2 * K; ++j) {
		// Compute correlation between column j and data
		double numerator = 0.0;
		double denominator = 0.0;
		for (int i = 0; i < n; ++i) {
			numerator += X[i][j] * data[i];
			denominator += X[i][j] * X[i][j];
		}
		coeffs[j] = (denominator > 1e-10) ? (numerator / denominator) : 0.0;
	}
	
	// Store coefficients for forecasting
	FourierCoeffs fc;
	fc.K = K;
	fc.sin_coeffs.resize(K);
	fc.cos_coeffs.resize(K);
	for (int k = 0; k < K; ++k) {
		fc.sin_coeffs[k] = coeffs[2 * k];
		fc.cos_coeffs[k] = coeffs[2 * k + 1];
	}
	fourier_coeffs_[period] = fc;
	
	// Reconstruct seasonal component
	std::vector<double> seasonal(n, 0.0);
	for (int i = 0; i < n; ++i) {
		for (int k = 1; k <= K; ++k) {
			double angle = 2.0 * PI * k * i / period;
			seasonal[i] += coeffs[2 * (k - 1)] * std::sin(angle);
			seasonal[i] += coeffs[2 * (k - 1) + 1] * std::cos(angle);
		}
	}
	
	return seasonal;
}

std::vector<double> MFLES::fitESLevel(const std::vector<double>& data) {
	const int n = static_cast<int>(data.size());
	
	if (n == 0) {
		return {};
	}
	
	// Initialize level as mean of first few observations
	int init_window = std::min(5, n);
	double level = 0.0;
	for (int i = 0; i < init_window; ++i) {
		level += data[i];
	}
	level /= init_window;
	
	// Store for forecasting
	es_level_ = level;
	
	// Apply exponential smoothing
	std::vector<double> es_values(n);
	for (int i = 0; i < n; ++i) {
		es_values[i] = level;
		level = es_alpha_ * data[i] + (1.0 - es_alpha_) * level;
	}
	
	// Update final level
	es_level_ = level;
	
	return es_values;
}

int MFLES::optimalK(int period) const {
	// Use K = min(period/2, MAX_FOURIER_TERMS)
	// This balances model complexity with capturing seasonality
	int k = std::min(period / 2, MAX_FOURIER_TERMS);
	return std::max(1, k);  // At least 1 Fourier pair
}

std::vector<double> MFLES::projectFourier(int period, int horizon, int start_index) {
	std::vector<double> forecast(horizon, 0.0);
	
	auto it = fourier_coeffs_.find(period);
	if (it == fourier_coeffs_.end()) {
		return forecast;  // No coefficients, return zeros
	}
	
	const FourierCoeffs& fc = it->second;
	const int n = static_cast<int>(history_.size());
	
	for (int h = 0; h < horizon; ++h) {
		int t = n + start_index + h;
		for (int k = 1; k <= fc.K; ++k) {
			double angle = 2.0 * PI * k * t / period;
			forecast[h] += fc.sin_coeffs[k - 1] * std::sin(angle);
			forecast[h] += fc.cos_coeffs[k - 1] * std::cos(angle);
		}
	}
	
	return forecast;
}

std::vector<double> MFLES::projectTrend(int horizon, int start_index) {
	std::vector<double> forecast(horizon);
	const int n = static_cast<int>(history_.size());
	
	for (int h = 0; h < horizon; ++h) {
		int t = n + start_index + h;
		forecast[h] = trend_intercept_ + trend_slope_ * t;
	}
	
	return forecast;
}

std::vector<double> MFLES::projectLevel(int horizon) {
	// Level is constant for forecast
	return std::vector<double>(horizon, es_level_);
}

void MFLES::computeFittedValues() {
	const int n = static_cast<int>(history_.size());
	fitted_.resize(n);
	residuals_.resize(n);
	
	for (int i = 0; i < n; ++i) {
		fitted_[i] = trend_component_[i] + level_component_[i];
		
		// Add all seasonal components
		for (const auto& [period, seasonal] : seasonal_components_) {
			fitted_[i] += seasonal[i];
		}
		
		residuals_[i] = history_[i] - fitted_[i];
	}
}

core::Forecast MFLES::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("MFLES: Must call fit() before predict()");
	}
	
	if (horizon <= 0) {
		throw std::invalid_argument("MFLES: horizon must be positive");
	}
	
	// Initialize forecast with zeros
	std::vector<double> forecast_values(horizon, 0.0);
	
	// Add trend component
	auto trend_forecast = projectTrend(horizon, 0);
	for (int h = 0; h < horizon; ++h) {
		forecast_values[h] += trend_forecast[h];
	}
	
	// Add seasonal components
	for (int period : seasonal_periods_) {
		auto seasonal_forecast = projectFourier(period, horizon, 0);
		for (int h = 0; h < horizon; ++h) {
			forecast_values[h] += seasonal_forecast[h];
		}
	}
	
	// Add level component
	auto level_forecast = projectLevel(horizon);
	for (int h = 0; h < horizon; ++h) {
		forecast_values[h] += level_forecast[h];
	}
	
	// Create and return forecast
	core::Forecast forecast;
	forecast.primary() = std::move(forecast_values);
	
	return forecast;
}

} // namespace anofoxtime::models

