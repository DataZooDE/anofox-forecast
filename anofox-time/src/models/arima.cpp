#include "anofox-time/models/arima.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <sstream>
#include <utility>

namespace anofoxtime::models {

namespace {

Eigen::VectorXd autocorr(const std::vector<double> &data, int max_lag) {
	const int n = static_cast<int>(data.size());
	Eigen::VectorXd acf = Eigen::VectorXd::Zero(max_lag + 1);

	if (n == 0) {
		return acf;
	}

	const double mean = std::accumulate(data.begin(), data.end(), 0.0) / static_cast<double>(n);
	double variance = 0.0;
	for (double val : data) {
		const double diff = val - mean;
		variance += diff * diff;
	}

	if (variance == 0.0) {
		return acf;
	}

	acf[0] = 1.0;
	for (int lag = 1; lag <= max_lag; ++lag) {
		double covariance = 0.0;
		for (int i = lag; i < n; ++i) {
			covariance += (data[i] - mean) * (data[i - lag] - mean);
		}
		acf[lag] = covariance / variance;
	}
	return acf;
}

Eigen::VectorXd estimate_ar_params(const std::vector<double> &data, int p) {
	if (p == 0)
		return {};
	if (static_cast<int>(data.size()) <= p) {
		throw std::invalid_argument("Not enough data to estimate AR parameters.");
	}

	const Eigen::VectorXd acf = autocorr(data, p);
	Eigen::MatrixXd R = Eigen::MatrixXd::Zero(p, p);
	for (int i = 0; i < p; ++i) {
		for (int j = 0; j < p; ++j) {
			R(i, j) = acf[std::abs(i - j)];
		}
	}
	const Eigen::VectorXd r = acf.segment(1, p);
	return R.colPivHouseholderQr().solve(r);
}

void fit_ma_component(const std::vector<double> &residuals, Eigen::VectorXd &ma_coeffs) {
	const int q = static_cast<int>(ma_coeffs.size());
	if (q == 0) {
		return;
	}

	const size_t n = residuals.size();
	for (int idx = 0; idx < q; ++idx) {
		const size_t lag = static_cast<size_t>(idx + 1);
		double numerator = 0.0;
		double denominator = 0.0;

		for (size_t t = lag; t < n; ++t) {
			numerator += residuals[t] * residuals[t - lag];
			denominator += residuals[t] * residuals[t];
		}

		if (denominator == 0.0) {
			ma_coeffs[idx] = 0.0;
			continue;
		}

		ma_coeffs[idx] = numerator / denominator;

		if (!std::isfinite(ma_coeffs[idx])) {
			throw std::runtime_error("Invalid MA coefficient detected during estimation.");
		}

		ma_coeffs[idx] = std::clamp(ma_coeffs[idx], -0.99, 0.99);
	}
}

double compute_residual_std(const std::vector<double> &residuals, size_t skip) {
	if (residuals.size() <= skip + 1) {
		return 0.0;
	}
	const size_t start = std::min(skip, residuals.size());
	const size_t count = residuals.size() - start;
	if (count == 0) {
		return 0.0;
	}

	const double mean = std::accumulate(residuals.begin() + start, residuals.end(), 0.0) /
	                    static_cast<double>(count);
	double variance = 0.0;
	for (size_t i = start; i < residuals.size(); ++i) {
		const double diff = residuals[i] - mean;
		variance += diff * diff;
	}

	const double denom = static_cast<double>(count - 1);
	return denom > 0.0 ? std::sqrt(variance / denom) : 0.0;
}

} // namespace

ARIMA::ARIMA(int p, int d, int q, int P, int D, int Q, int s, bool include_intercept) 
    : p_(p), d_(d), q_(q), P_(P), D_(D), Q_(Q), seasonal_period_(s), include_intercept_(include_intercept) {
	if (p < 0 || d < 0 || q < 0) {
		throw std::invalid_argument("ARIMA orders (p, d, q) must be non-negative.");
	}
	if (P < 0 || D < 0 || Q < 0) {
		throw std::invalid_argument("Seasonal ARIMA orders (P, D, Q) must be non-negative.");
	}
	if (seasonal_period_ < 0) {
		throw std::invalid_argument("Seasonal period must be non-negative.");
	}
	if ((P > 0 || Q > 0 || D > 0) && seasonal_period_ < 2) {
		throw std::invalid_argument("Seasonal period must be >= 2 for seasonal ARIMA components.");
	}
	if (p == 0 && q == 0 && P == 0 && Q == 0) {
		throw std::invalid_argument("At least one of p, q, P, or Q must be greater than zero for ARIMA.");
	}
}

std::vector<double> ARIMA::difference(const std::vector<double> &data, int d) {
	if (d == 0)
		return data;
	if (data.size() <= static_cast<size_t>(d)) {
		throw std::invalid_argument("Insufficient data length for requested differencing order.");
	}

	std::vector<double> result = data;
	for (int diff_order = 0; diff_order < d; ++diff_order) {
		std::vector<double> temp;
		temp.reserve(result.size() > 0 ? result.size() - 1 : 0);
		for (size_t i = 1; i < result.size(); ++i) {
			temp.push_back(result[i] - result[i - 1]);
		}
		result = std::move(temp);
		if (result.empty()) {
			break;
		}
	}
	return result;
}

std::vector<double> ARIMA::integrate(const std::vector<double> &forecast_diff, const std::vector<double> &last_values,
                                     int d) {
	if (d == 0)
		return forecast_diff;
	if (last_values.size() < static_cast<size_t>(d + 1)) {
		throw std::invalid_argument("Insufficient history retained to integrate differenced forecast.");
	}

	std::vector<double> result = forecast_diff;
	for (int level = 0; level < d; ++level) {
		std::vector<double> integrated;
		integrated.reserve(result.size());

		double previous = last_values[last_values.size() - static_cast<size_t>(level) - 1];
		for (double val : result) {
			previous += val;
			integrated.push_back(previous);
		}
		result = std::move(integrated);
	}
	return result;
}

std::vector<double> ARIMA::seasonalDifference(const std::vector<double> &data, int D, int s) {
	if (D == 0 || s <= 1)
		return data;
	if (data.size() <= static_cast<size_t>(D * s)) {
		throw std::invalid_argument("Insufficient data length for requested seasonal differencing order.");
	}

	std::vector<double> result = data;
	for (int diff_order = 0; diff_order < D; ++diff_order) {
		std::vector<double> temp;
		const size_t lag = static_cast<size_t>(s);
		if (result.size() <= lag) {
			break;
		}
		temp.reserve(result.size() - lag);
		for (size_t i = lag; i < result.size(); ++i) {
			temp.push_back(result[i] - result[i - lag]);
		}
		result = std::move(temp);
		if (result.empty()) {
			break;
		}
	}
	return result;
}

std::vector<double> ARIMA::seasonalIntegrate(const std::vector<double> &forecast_diff,
                                             const std::vector<double> &last_values, int D, int s) {
	if (D == 0 || s <= 1)
		return forecast_diff;
	if (last_values.size() < static_cast<size_t>(D * s + 1)) {
		throw std::invalid_argument("Insufficient history retained to integrate seasonal differenced forecast.");
	}

	std::vector<double> result = forecast_diff;
	for (int level = 0; level < D; ++level) {
		std::vector<double> integrated;
		integrated.reserve(result.size());

		// Get the appropriate lag values from history
		const size_t lag = static_cast<size_t>(s);
		
		// For seasonal integration: y_t = diff_t + y_{t-s}
		// For first s forecast steps, use historical values
		// For steps beyond s, use previously forecasted values
		for (size_t h = 0; h < result.size(); ++h) {
			if (h < lag) {
				// Use historical values (last s values before forecast)
				// The last lag values are at the end of last_values
				size_t history_idx = last_values.size() - lag + h;
				if (history_idx < last_values.size()) {
					integrated.push_back(result[h] + last_values[history_idx]);
				} else {
					// Fallback: just use the differenced value
					integrated.push_back(result[h]);
				}
			} else {
				// Use previously forecasted (integrated) values
				// y_t = diff_t + y_{t-s}
				integrated.push_back(result[h] + integrated[h - lag]);
			}
		}
		result = std::move(integrated);
	}
	return result;
}

std::vector<double> ARIMA::combinedDifference(const std::vector<double> &data, int d, int D, int s) {
	// Apply non-seasonal differencing first, then seasonal
	std::vector<double> result = difference(data, d);
	result = seasonalDifference(result, D, s);
	return result;
}

double ARIMA::normalQuantile(double p) {
	if (p <= 0.0) {
		return -std::numeric_limits<double>::infinity();
	}
	if (p >= 1.0) {
		return std::numeric_limits<double>::infinity();
	}
	if (std::abs(p - 0.5) < 1e-10) {
		return 0.0;
	}

	static const double a[] = {-3.969683028665376e1, 2.209460984245205e2,  -2.759285104469687e2,
	                           1.38357751867269e2,   -3.066479806614716e1, 2.506628277459239};
	static const double b[] = {-5.447609879822406e1, 1.615858368580409e2, -1.556989798598866e2, 6.680131188771972e1,
	                           -1.328068155288572e1};
	static const double c[] = {-7.784894002430293e-3, -3.223964580411365e-1, -2.400758277161838,
	                           -2.549732539343734,    4.374664141464968,     2.938163982698783};
	static const double d[] = {7.784695709041462e-3, 3.224671290700398e-1, 2.445134137142996, 3.754408661907416};
	constexpr double p_low = 0.02425;
	constexpr double p_high = 1.0 - p_low;

	if (p < p_low) {
		const double q = std::sqrt(-2.0 * std::log(p));
		return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
		       ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
	}
	if (p <= p_high) {
		const double q = p - 0.5;
		const double r = q * q;
		return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q /
		       (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1.0);
	}

	const double q = std::sqrt(-2.0 * std::log(1.0 - p));
	return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
	       ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
}

double ARIMA::logLikelihood(const std::vector<double> &residuals) {
	if (residuals.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}

	double sum_sq = 0.0;
	for (double r : residuals) {
		sum_sq += r * r;
	}

	const double sigma2 = sum_sq / static_cast<double>(residuals.size());
	if (sigma2 <= 0.0) {
		return std::numeric_limits<double>::quiet_NaN();
	}

	return -0.5 * static_cast<double>(residuals.size()) * (std::log(2.0 * M_PI * sigma2) + 1.0);
}

void ARIMA::fit(const core::TimeSeries &ts) {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("ARIMA currently supports univariate series only.");
	}
	
	// Calculate minimum required data size
	const int seasonal_lag = (seasonal_period_ > 1) ? P_ * seasonal_period_ : 0;
	const size_t min_required = static_cast<size_t>(std::max({p_ + d_, q_, seasonal_lag + D_ * seasonal_period_, Q_ * seasonal_period_}) + 1);
	if (ts.size() < min_required) {
		throw std::invalid_argument("Insufficient data for the given SARIMA order.");
	}

	history_ = ts.getValues();
	
	// Apply combined differencing (non-seasonal then seasonal)
	// Step 1: Non-seasonal differencing
	std::vector<double> nonseasonal_diff = difference(history_, d_);
	
	// Step 2: Seasonal differencing of the non-seasonally differenced series
	differenced_history_ = seasonalDifference(nonseasonal_diff, D_, seasonal_period_);
	if (differenced_history_.empty()) {
		throw std::invalid_argument("Differencing removed all observations.");
	}

	const size_t n = differenced_history_.size();
	const int max_nonseasonal_lag = std::max(p_, q_);
	const int max_seasonal_lag = (seasonal_period_ > 1) ? std::max(P_, Q_) * seasonal_period_ : 0;
	const int max_lag = std::max(max_nonseasonal_lag, max_seasonal_lag);

	// Store last values for integration
	// For integration, we need to reverse the differencing:
	// - To integrate seasonally, we need last values from the NON-SEASONALLY differenced series
	// - To integrate non-seasonally, we need last values from the ORIGINAL series
	if (d_ > 0 || D_ > 0) {
		const size_t total_diff = static_cast<size_t>(d_ + D_ * seasonal_period_);
		const size_t retain = total_diff + static_cast<size_t>(seasonal_period_) + 1;
		const size_t start_idx = history_.size() > retain ? history_.size() - retain : 0;
		last_values_.assign(history_.begin() + start_idx, history_.end());
		
		// For seasonal integration, use values from the non-seasonally differenced series
		if (D_ > 0 && seasonal_period_ > 1) {
			const size_t seasonal_retain = static_cast<size_t>(D_ * seasonal_period_ + seasonal_period_);
			const size_t seasonal_start = nonseasonal_diff.size() > seasonal_retain ? nonseasonal_diff.size() - seasonal_retain : 0;
			seasonal_last_values_.assign(nonseasonal_diff.begin() + seasonal_start, nonseasonal_diff.end());
		}
	} else {
		last_values_.clear();
		seasonal_last_values_.clear();
	}

	// Initialize coefficient vectors
	if (p_ > 0) {
		ar_coeffs_ = estimate_ar_params(differenced_history_, p_);
	} else {
		ar_coeffs_.resize(0);
	}

	if (q_ > 0) {
		ma_coeffs_ = Eigen::VectorXd::Zero(q_);
	} else {
		ma_coeffs_.resize(0);
	}

	if (P_ > 0 && seasonal_period_ > 1) {
		seasonal_ar_coeffs_ = Eigen::VectorXd::Zero(P_);
		// Simple initialization for seasonal AR
		for (int i = 0; i < P_; ++i) {
			seasonal_ar_coeffs_[i] = 0.1 / static_cast<double>(i + 1);
		}
	} else {
		seasonal_ar_coeffs_.resize(0);
	}

	if (Q_ > 0 && seasonal_period_ > 1) {
		seasonal_ma_coeffs_ = Eigen::VectorXd::Zero(Q_);
	} else {
		seasonal_ma_coeffs_.resize(0);
	}

	mean_ = std::accumulate(differenced_history_.begin(), differenced_history_.end(), 0.0) / static_cast<double>(n);

	residuals_.assign(n, 0.0);

	// Initial residual computation
	for (size_t t = static_cast<size_t>(max_lag); t < n; ++t) {
		double prediction = include_intercept_ ? mean_ : 0.0;
		
		// Non-seasonal AR
		for (int i = 0; i < p_; ++i) {
			if (t > static_cast<size_t>(i)) {
				prediction += ar_coeffs_[i] * differenced_history_[t - static_cast<size_t>(i) - 1];
			}
		}
		
		// Seasonal AR
		for (int i = 0; i < P_; ++i) {
			const size_t seasonal_lag = static_cast<size_t>((i + 1) * seasonal_period_);
			if (t >= seasonal_lag) {
				prediction += seasonal_ar_coeffs_[i] * differenced_history_[t - seasonal_lag];
			}
		}
		
		residuals_[t] = differenced_history_[t] - prediction;
	}

	// Fit MA components
	if (q_ > 0) {
		fit_ma_component(residuals_, ma_coeffs_);
	}
	
	// Fit seasonal MA component (simple approach)
	if (Q_ > 0 && seasonal_period_ > 1) {
		for (int idx = 0; idx < Q_; ++idx) {
			const size_t lag = static_cast<size_t>((idx + 1) * seasonal_period_);
			double numerator = 0.0;
			double denominator = 0.0;

			for (size_t t = lag; t < n; ++t) {
				numerator += residuals_[t] * residuals_[t - lag];
				denominator += residuals_[t] * residuals_[t];
			}

			if (denominator == 0.0) {
				seasonal_ma_coeffs_[idx] = 0.0;
				continue;
			}

			seasonal_ma_coeffs_[idx] = numerator / denominator;
			seasonal_ma_coeffs_[idx] = std::clamp(seasonal_ma_coeffs_[idx], -0.99, 0.99);
		}
	}

	// Iterative refinement
	for (int iteration = 0; iteration < 5; ++iteration) {
		for (size_t t = static_cast<size_t>(max_lag); t < n; ++t) {
			double prediction = include_intercept_ ? mean_ : 0.0;
			
			// Non-seasonal AR
			for (int i = 0; i < p_; ++i) {
				if (t > static_cast<size_t>(i)) {
					prediction += ar_coeffs_[i] * differenced_history_[t - static_cast<size_t>(i) - 1];
				}
			}
			
			// Seasonal AR
			for (int i = 0; i < P_; ++i) {
				const size_t seasonal_lag = static_cast<size_t>((i + 1) * seasonal_period_);
				if (t >= seasonal_lag) {
					prediction += seasonal_ar_coeffs_[i] * differenced_history_[t - seasonal_lag];
				}
			}
			
			// Non-seasonal MA
			for (int i = 0; i < q_; ++i) {
				if (t > static_cast<size_t>(i)) {
					prediction += ma_coeffs_[i] * residuals_[t - static_cast<size_t>(i) - 1];
				}
			}
			
			// Seasonal MA
			for (int i = 0; i < Q_; ++i) {
				const size_t seasonal_lag = static_cast<size_t>((i + 1) * seasonal_period_);
				if (t >= seasonal_lag) {
					prediction += seasonal_ma_coeffs_[i] * residuals_[t - seasonal_lag];
				}
			}
			
			residuals_[t] = differenced_history_[t] - prediction;
		}

		// Re-estimate AR coefficients
		if (p_ > 0) {
			ar_coeffs_ = estimate_ar_params(differenced_history_, p_);
		}
		
		// Re-estimate MA coefficients
		if (q_ > 0) {
			fit_ma_component(residuals_, ma_coeffs_);
		}
		
		// Re-estimate seasonal MA (seasonal AR uses simpler estimation)
		if (Q_ > 0 && seasonal_period_ > 1) {
			for (int idx = 0; idx < Q_; ++idx) {
				const size_t lag = static_cast<size_t>((idx + 1) * seasonal_period_);
				double numerator = 0.0;
				double denominator = 0.0;

				for (size_t t = lag; t < n; ++t) {
					numerator += residuals_[t] * residuals_[t - lag];
					denominator += residuals_[t] * residuals_[t];
				}

				if (denominator != 0.0) {
					seasonal_ma_coeffs_[idx] = numerator / denominator;
					seasonal_ma_coeffs_[idx] = std::clamp(seasonal_ma_coeffs_[idx], -0.99, 0.99);
				}
			}
		}
	}

	// Compute fitted values
	fitted_values_.assign(static_cast<size_t>(max_lag), std::numeric_limits<double>::quiet_NaN());
	for (size_t t = static_cast<size_t>(max_lag); t < n; ++t) {
		double prediction = include_intercept_ ? mean_ : 0.0;
		
		// Non-seasonal AR
		for (int i = 0; i < p_; ++i) {
			if (t > static_cast<size_t>(i)) {
				prediction += ar_coeffs_[i] * differenced_history_[t - static_cast<size_t>(i) - 1];
			}
		}
		
		// Seasonal AR
		for (int i = 0; i < P_; ++i) {
			const size_t seasonal_lag = static_cast<size_t>((i + 1) * seasonal_period_);
			if (t >= seasonal_lag) {
				prediction += seasonal_ar_coeffs_[i] * differenced_history_[t - seasonal_lag];
			}
		}
		
		// Non-seasonal MA
		for (int i = 0; i < q_; ++i) {
			if (t > static_cast<size_t>(i)) {
				prediction += ma_coeffs_[i] * residuals_[t - static_cast<size_t>(i) - 1];
			}
		}
		
		// Seasonal MA
		for (int i = 0; i < Q_; ++i) {
			const size_t seasonal_lag = static_cast<size_t>((i + 1) * seasonal_period_);
			if (t >= seasonal_lag) {
				prediction += seasonal_ma_coeffs_[i] * residuals_[t - seasonal_lag];
			}
		}
		
		fitted_values_.push_back(prediction);
	}

	// Compute intercept (adjusted for seasonal AR if present)
	double ar_sum = ar_coeffs_.sum();
	double seasonal_ar_sum = seasonal_ar_coeffs_.sum();
	intercept_ = include_intercept_ ? mean_ * (1.0 - ar_sum - seasonal_ar_sum) : 0.0;

	// Store last residuals for forecasting
	if (q_ > 0) {
		const size_t retain_q = std::min(residuals_.size(), static_cast<size_t>(q_));
		last_residuals_.assign(residuals_.end() - retain_q, residuals_.end());
	} else {
		last_residuals_.clear();
	}
	
	if (Q_ > 0 && seasonal_period_ > 1) {
		const size_t retain_Q = std::min(residuals_.size(), static_cast<size_t>(Q_ * seasonal_period_));
		seasonal_last_residuals_.assign(residuals_.end() - retain_Q, residuals_.end());
	} else {
		seasonal_last_residuals_.clear();
	}

	double sum_sq = 0.0;
	for (double r : residuals_) {
		sum_sq += r * r;
	}
	sigma2_ = sum_sq / static_cast<double>(residuals_.size());

	const double loglik = logLikelihood(residuals_);
	if (std::isfinite(loglik)) {
		const int k = p_ + q_ + P_ + Q_ + (include_intercept_ ? 1 : 0);
		aic_ = -2.0 * loglik + 2.0 * static_cast<double>(k);
		if (!residuals_.empty()) {
			bic_ = -2.0 * loglik + static_cast<double>(k) * std::log(static_cast<double>(residuals_.size()));
		} else {
			bic_.reset();
		}
	} else {
		aic_.reset();
		bic_.reset();
	}

	is_fitted_ = true;
	
	// Log model info
	if (seasonal_period_ > 1 && (P_ > 0 || D_ > 0 || Q_ > 0)) {
		ANOFOX_INFO("SARIMA({},{},{})({},,{})[{}] model fitted.", p_, d_, q_, P_, D_, Q_, seasonal_period_);
	} else {
		ANOFOX_INFO("ARIMA({},{},{}) model fitted.", p_, d_, q_);
	}
	
	if (p_ > 0) {
		std::stringstream ss;
		ss << ar_coeffs_.transpose();
		ANOFOX_DEBUG("Non-seasonal AR coeffs: [{}]", ss.str());
	}
    if (q_ > 0) {
        std::stringstream ss;
        ss << ma_coeffs_.transpose();
        ANOFOX_DEBUG("Non-seasonal MA coeffs: [{}]", ss.str());
    }
    if (P_ > 0) {
        std::stringstream ss;
        ss << seasonal_ar_coeffs_.transpose();
        ANOFOX_DEBUG("Seasonal AR coeffs: [{}]", ss.str());
    }
    if (Q_ > 0) {
        std::stringstream ss;
        ss << seasonal_ma_coeffs_.transpose();
        ANOFOX_DEBUG("Seasonal MA coeffs: [{}]", ss.str());
    }
    ANOFOX_DEBUG("Intercept: {}", intercept_);
    if (aic_) {
        if (bic_) {
            ANOFOX_INFO("ARIMA diagnostics: AIC = {:.6f}, BIC = {:.6f}", *aic_, *bic_);
        } else {
            ANOFOX_INFO("ARIMA diagnostics: AIC = {:.6f}", *aic_);
        }
    }
}

core::Forecast ARIMA::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("Predict called before fit.");
	}
	if (horizon <= 0) {
		return {};
	}

	const bool needs_ar = p_ > 0;
	const bool needs_ma = q_ > 0;
	const bool needs_seasonal_ar = P_ > 0 && seasonal_period_ > 1;
	const bool needs_seasonal_ma = Q_ > 0 && seasonal_period_ > 1;
	
	if (needs_ar && static_cast<int>(differenced_history_.size()) < p_) {
		throw std::runtime_error("Differenced history shorter than AR order; cannot forecast.");
	}

	std::vector<double> diff_forecast;
	diff_forecast.reserve(static_cast<size_t>(horizon));

	// Initialize with sufficient history for seasonal lags
	std::vector<double> temp_history = differenced_history_;
	std::vector<double> temp_residuals;
	
	// For seasonal MA, we need more residuals
	if (needs_seasonal_ma) {
		temp_residuals = seasonal_last_residuals_;
	} else if (needs_ma) {
		temp_residuals = last_residuals_;
	}

	for (int h = 0; h < horizon; ++h) {
		double next_value = intercept_;

		// Non-seasonal AR terms
		if (needs_ar) {
			for (int i = 0; i < p_; ++i) {
				if (temp_history.size() > static_cast<size_t>(i)) {
					const size_t index = temp_history.size() - static_cast<size_t>(i) - 1;
					next_value += ar_coeffs_[i] * temp_history[index];
				}
			}
		}

		// Seasonal AR terms
		if (needs_seasonal_ar) {
			for (int i = 0; i < P_; ++i) {
				const size_t seasonal_lag = static_cast<size_t>((i + 1) * seasonal_period_);
				if (temp_history.size() >= seasonal_lag) {
					const size_t index = temp_history.size() - seasonal_lag;
					next_value += seasonal_ar_coeffs_[i] * temp_history[index];
				}
			}
		}

		// Non-seasonal MA terms
		if (needs_ma && !temp_residuals.empty()) {
			for (int i = 0; i < q_; ++i) {
				if (temp_residuals.size() > static_cast<size_t>(i)) {
					const size_t index = temp_residuals.size() - static_cast<size_t>(i) - 1;
					next_value += ma_coeffs_[i] * temp_residuals[index];
				}
			}
		}

		// Seasonal MA terms
		if (needs_seasonal_ma && !temp_residuals.empty()) {
			for (int i = 0; i < Q_; ++i) {
				const size_t seasonal_lag = static_cast<size_t>((i + 1) * seasonal_period_);
				if (temp_residuals.size() >= seasonal_lag) {
					const size_t index = temp_residuals.size() - seasonal_lag;
					next_value += seasonal_ma_coeffs_[i] * temp_residuals[index];
				}
			}
		}

		diff_forecast.push_back(next_value);
		temp_history.push_back(next_value);
		if (needs_ma || needs_seasonal_ma) {
			temp_residuals.push_back(0.0);
		}
	}

	// Apply integration (seasonal first, then non-seasonal - reverse of differencing)
	std::vector<double> result = diff_forecast;
	if (D_ > 0 && seasonal_period_ > 1) {
		result = seasonalIntegrate(result, seasonal_last_values_.empty() ? last_values_ : seasonal_last_values_, D_, seasonal_period_);
	}
	if (d_ > 0) {
		result = integrate(result, last_values_, d_);
	}
	if (D_ == 0 && d_ == 0) {
		result = diff_forecast;
	}
	
	core::Forecast forecast;
	forecast.primary() = result;
	return forecast;
}

core::Forecast ARIMA::predictWithConfidence(int horizon, double confidence) {
	if (confidence <= 0.0 || confidence >= 1.0) {
		throw std::invalid_argument("Confidence level must be between 0 and 1.");
	}

	core::Forecast forecast = predict(horizon);
	auto &primary = forecast.primary();
	if (primary.empty()) {
		return forecast;
	}

	const double residual_std = compute_residual_std(residuals_, static_cast<size_t>(std::max(p_, q_)));
	if (residual_std <= 0.0) {
		ANOFOX_WARN("Residual standard deviation is non-positive; confidence bounds will be point forecasts.");
		forecast.lowerSeries() = primary;
		forecast.upperSeries() = primary;
		return forecast;
	}

	const double alpha = 1.0 - confidence;
	const double z_score = normalQuantile(1.0 - alpha / 2.0);

	auto &lower = forecast.lowerSeries();
	auto &upper = forecast.upperSeries();
	lower.reserve(primary.size());
	upper.reserve(primary.size());

	for (size_t idx = 0; idx < primary.size(); ++idx) {
		const double scale = residual_std * std::sqrt(1.0 + static_cast<double>(idx) * 0.1);
		lower.push_back(primary[idx] - z_score * scale);
		upper.push_back(primary[idx] + z_score * scale);
	}

	return forecast;
}

ARIMABuilder &ARIMABuilder::withAR(int p) {
	p_ = p;
	return *this;
}

ARIMABuilder &ARIMABuilder::withDifferencing(int d) {
	d_ = d;
	return *this;
}

ARIMABuilder &ARIMABuilder::withMA(int q) {
	q_ = q;
	return *this;
}

ARIMABuilder &ARIMABuilder::withSeasonalAR(int P) {
	P_ = P;
	return *this;
}

ARIMABuilder &ARIMABuilder::withSeasonalDifferencing(int D) {
	D_ = D;
	return *this;
}

ARIMABuilder &ARIMABuilder::withSeasonalMA(int Q) {
	Q_ = Q;
	return *this;
}

ARIMABuilder &ARIMABuilder::withSeasonalPeriod(int s) {
	s_ = s;
	return *this;
}

ARIMABuilder &ARIMABuilder::withIntercept(bool include_intercept) {
	include_intercept_ = include_intercept;
	return *this;
}

std::unique_ptr<ARIMA> ARIMABuilder::build() {
	return std::unique_ptr<ARIMA>(new ARIMA(p_, d_, q_, P_, D_, Q_, s_, include_intercept_));
}

} // namespace anofoxtime::models
