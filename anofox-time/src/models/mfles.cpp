#include "anofox-time/models/mfles.hpp"
#include "anofox-time/utils/robust_regression.hpp"
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <algorithm>
#include <limits>

namespace anofoxtime::models {

namespace {
	constexpr double PI = 3.14159265358979323846;
	constexpr int MAX_FOURIER_TERMS = 10;
	constexpr double EPSILON = 1e-10;
}

// ============================================================================
// Constructors
// ============================================================================

MFLES::MFLES()
	: MFLES(Params{})
{
}

MFLES::MFLES(const Params& params)
	: params_(params)
{
	// Validate parameters
	if (params_.max_rounds < 1) {
		throw std::invalid_argument("MFLES: max_rounds must be at least 1");
	}

	// Validate learning rates
	auto validate_lr = [](double lr, const char* name) {
		if (lr < 0.0 || lr > 1.0) {
			throw std::invalid_argument(std::string("MFLES: ") + name + " must be in [0, 1]");
		}
	};

	validate_lr(params_.lr_median, "lr_median");
	validate_lr(params_.lr_trend, "lr_trend");
	validate_lr(params_.lr_season, "lr_season");
	validate_lr(params_.lr_rs, "lr_rs");
	validate_lr(params_.lr_exogenous, "lr_exogenous");

	// Validate seasonal periods
	for (int period : params_.seasonal_periods) {
		if (period < 1) {
			throw std::invalid_argument("MFLES: seasonal periods must be >= 1");
		}
	}

	// Validate other parameters
	if (params_.cov_threshold < 0.0 || params_.cov_threshold > 1.0) {
		throw std::invalid_argument("MFLES: cov_threshold must be in [0, 1]");
	}

	if (params_.n_changepoints_pct < 0.0 || params_.n_changepoints_pct > 1.0) {
		throw std::invalid_argument("MFLES: n_changepoints_pct must be in [0, 1]");
	}
}

// ============================================================================
// Main fit() method
// ============================================================================

void MFLES::fit(const core::TimeSeries& ts) {
	original_data_ = ts.getValues();
	const int n = static_cast<int>(original_data_.size());

	if (n < 3) {
		throw std::runtime_error("MFLES requires at least 3 data points");
	}

	// Step 1: Preprocess data (multiplicative mode, normalization)
	preprocess(original_data_);

	// Step 2: Initialize components
	trend_component_ = std::vector<double>(n, 0.0);
	level_component_ = std::vector<double>(n, 0.0);
	for (int period : params_.seasonal_periods) {
		seasonal_components_[period] = std::vector<double>(n, 0.0);
	}

	// Initialize residuals with preprocessed data
	std::vector<double> residuals = preprocessed_data_;

	// Step 3: Initialize accumulated parameters
	std::map<int, FourierCoeffs> accumulated_fourier;
	double accumulated_level = 0.0;
	accumulated_trend_.clear();  // Will store last 2 fitted trend values

	// Step 4: Gradient boosting iterations (5-component system)
	for (int iter = 0; iter < params_.max_rounds; ++iter) {
		// Component 1: Median baseline (first iteration only, if enabled)
		// Phase 8 Fix #1: Handle median as vector instead of scalar
		if (iter == 0 && params_.lr_median > 0.0) {
			std::vector<double> median_comp = fitMedianComponent(residuals);

			// Initialize median_component_ if empty
			if (median_component_.empty()) {
				median_component_.resize(n, 0.0);
			}

			// Accumulate median component and update residuals
			for (int i = 0; i < n; ++i) {
				median_component_[i] += params_.lr_median * median_comp[i];
				residuals[i] -= params_.lr_median * median_comp[i];
			}
		}

		// Component 2: Trend (OLS, Siegel, or Piecewise)
		if (params_.lr_trend > 0.0) {
			std::vector<double> trend_fit;

			switch (params_.trend_method) {
				case TrendMethod::OLS:
					trend_fit = fitLinearTrend(residuals);
					break;
				case TrendMethod::SIEGEL_ROBUST:
					trend_fit = fitSiegelTrend(residuals);
					break;
				case TrendMethod::PIECEWISE:
					trend_fit = fitPiecewiseTrend(residuals);
					break;
			}

			// Accumulate last 2 fitted trend values (for projecting trend in forecast)
			// Store the last two values: trend_fit[n-2] and trend_fit[n-1]
			if (n >= 2) {
				double val_n_minus_2 = trend_fit[n-2];
				double val_n_minus_1 = trend_fit[n-1];

				if (accumulated_trend_.size() < 2) {
					// First iteration: initialize with last 2 values
					accumulated_trend_ = {val_n_minus_2, val_n_minus_1};
				} else {
					// Subsequent iterations: add to accumulated values
					accumulated_trend_[0] += params_.lr_trend * val_n_minus_2;
					accumulated_trend_[1] += params_.lr_trend * val_n_minus_1;
				}
			}

			// Update components and residuals
			for (int i = 0; i < n; ++i) {
				trend_component_[i] += params_.lr_trend * trend_fit[i];
				residuals[i] -= params_.lr_trend * trend_fit[i];
			}
		}

		// Component 3: Fourier seasonality (multiple periods, optionally weighted)
		if (params_.lr_season > 0.0) {
			for (int period : params_.seasonal_periods) {
				if (n >= 2 * period) {  // Need at least 2 cycles
					auto seasonal_fit = fitFourierSeason(residuals, period, params_.seasonality_weights);

					// Accumulate Fourier coefficients
					if (accumulated_fourier.find(period) == accumulated_fourier.end()) {
						accumulated_fourier[period] = fourier_coeffs_[period];
						// Scale by learning rate
						for (auto& c : accumulated_fourier[period].sin_coeffs) c *= params_.lr_season;
						for (auto& c : accumulated_fourier[period].cos_coeffs) c *= params_.lr_season;
					} else {
						// Add current coefficients (scaled by LR)
						for (size_t k = 0; k < fourier_coeffs_[period].sin_coeffs.size(); ++k) {
							accumulated_fourier[period].sin_coeffs[k] += params_.lr_season * fourier_coeffs_[period].sin_coeffs[k];
							accumulated_fourier[period].cos_coeffs[k] += params_.lr_season * fourier_coeffs_[period].cos_coeffs[k];
						}
					}

					// Update components and residuals
					for (int i = 0; i < n; ++i) {
						seasonal_components_[period][i] += params_.lr_season * seasonal_fit[i];
						residuals[i] -= params_.lr_season * seasonal_fit[i];
					}
				}
			}
		}

		// Component 4: Residual smoothing (ES ensemble or MA)
		// Phase 8 Fix #3: Apply round penalty threshold check (matches statsforecast line 602)
		if (params_.lr_rs > 0.0) {
			std::vector<double> rs_fit;

			if (params_.smoother) {
				// Moving average
				rs_fit = fitMovingAverage(residuals, params_.ma_window);
			} else {
				// ES ensemble (average multiple alphas)
				rs_fit = fitESEnsemble(residuals);
			}

			// Calculate MSE before and after applying residual smoothing
			double mse_before = 0.0;
			for (const auto& r : residuals) {
				mse_before += r * r;
			}
			mse_before /= n;

			// Calculate what MSE would be if we apply this component
			std::vector<double> residuals_after = residuals;
			for (int i = 0; i < n; ++i) {
				residuals_after[i] -= params_.lr_rs * rs_fit[i];
			}

			double mse_after = 0.0;
			for (const auto& r : residuals_after) {
				mse_after += r * r;
			}
			mse_after /= n;

			// Only accept if improvement exceeds round penalty threshold
			// statsforecast: if mse > component_mse + round_penalty * mse
			// (i.e., reject if new MSE is not better by at least round_penalty * old_mse)
			if (mse_after <= mse_before + params_.round_penalty * mse_before) {
				// Accept the component
				accumulated_level += params_.lr_rs * final_level_;

				// Update components and residuals
				for (int i = 0; i < n; ++i) {
					level_component_[i] += params_.lr_rs * rs_fit[i];
					residuals[i] -= params_.lr_rs * rs_fit[i];
				}
			}
			// else: reject component, don't update residuals or level
		}

		// Component 5: Exogenous variables (placeholder for future)
		// if (params_.lr_exogenous > 0.0 && has_exogenous_) { ... }

		// Outlier capping (after warmup rounds)
		if (params_.cap_outliers && iter >= params_.outlier_cap_start_round && iter % 5 == 0) {
			capOutliers(residuals);
		}

		// Early stopping: check residual convergence
		double residual_std = 0.0;
		for (const auto& r : residuals) {
			residual_std += r * r;
		}
		residual_std = std::sqrt(residual_std / n);

		double data_range = *std::max_element(preprocessed_data_.begin(), preprocessed_data_.end()) -
		                    *std::min_element(preprocessed_data_.begin(), preprocessed_data_.end());

		if (residual_std < params_.convergence_threshold * data_range && iter >= 5) {
			actual_rounds_ = iter + 1;
			break;
		}

		actual_rounds_ = iter + 1;
	}

	// Store accumulated parameters for forecasting
	// Note: accumulated_trend_ is already set during iterations (last 2 fitted values)
	fourier_coeffs_ = accumulated_fourier;
	final_level_ = accumulated_level;

	// Compute fitted values and residuals
	computeFittedValues();

	is_fitted_ = true;
}

// ============================================================================
// Preprocessing methods
// ============================================================================

void MFLES::preprocess(const std::vector<double>& data) {
	preprocessed_data_ = data;
	const int n = static_cast<int>(data.size());

	// Determine if we should use multiplicative mode
	if (params_.multiplicative_override.has_value()) {
		// User explicitly set mode
		is_multiplicative_ = params_.multiplicative_override.value();
	} else {
		// Auto-detect based on CoV and seasonality presence
		is_multiplicative_ = shouldUseMultiplicative(data);
	}

	// Phase 7: CRITICAL FIX - Match statsforecast preprocessing order
	// Step 1: ALWAYS compute mean and std from ORIGINAL data (before any transforms)
	mean_ = std::accumulate(data.begin(), data.end(), 0.0) / n;

	double variance = 0.0;
	for (const auto& val : data) {
		variance += (val - mean_) * (val - mean_);
	}
	std_ = std::sqrt(variance / n);

	// Avoid division by zero
	if (std_ < EPSILON) {
		std_ = 1.0;
	}

	// Step 2: ALWAYS apply z-score normalization FIRST
	for (auto& val : preprocessed_data_) {
		val = (val - mean_) / std_;
	}

	// Step 3: THEN apply log transform if multiplicative
	if (is_multiplicative_) {
		// Check if normalized data is positive
		double min_val = *std::min_element(preprocessed_data_.begin(), preprocessed_data_.end());

		if (min_val > 0) {
			// Log transform AFTER normalization
			for (auto& val : preprocessed_data_) {
				val = std::log(val);
			}
		} else {
			// Cannot use multiplicative with non-positive normalized data
			// This should be extremely rare after z-score normalization
			is_multiplicative_ = false;
		}
	}
}

void MFLES::postprocess(std::vector<double>& forecasts) const {
	// Phase 7: CRITICAL FIX - Reverse transformations in opposite order
	// This must exactly mirror the preprocess() order in reverse

	// Step 1: If multiplicative, reverse log transform FIRST
	if (is_multiplicative_) {
		for (auto& val : forecasts) {
			val = std::exp(val);
		}
	}

	// Step 2: ALWAYS reverse z-score normalization AFTER
	for (auto& val : forecasts) {
		val = val * std_ + mean_;
	}
}

bool MFLES::shouldUseMultiplicative(const std::vector<double>& data) const {
	// Auto-detect multiplicative mode based on:
	// 1. Data must be positive
	// 2. CoV (coefficient of variation) > threshold
	// 3. Seasonality must be present

	double min_val = *std::min_element(data.begin(), data.end());
	if (min_val <= 0) {
		return false;  // Cannot use log transform
	}

	// Check if seasonality is present
	bool has_seasonality = !params_.seasonal_periods.empty() && params_.seasonal_periods[0] > 1;
	if (!has_seasonality) {
		return false;
	}

	// Compute CoV
	double cov = computeCoV(data);
	return cov > params_.cov_threshold;
}

double MFLES::computeCoV(const std::vector<double>& data) const {
	// Coefficient of Variation = std / mean
	const int n = static_cast<int>(data.size());

	double mean = std::accumulate(data.begin(), data.end(), 0.0) / n;

	double variance = 0.0;
	for (const auto& val : data) {
		variance += (val - mean) * (val - mean);
	}
	double std = std::sqrt(variance / n);

	if (std::abs(mean) < EPSILON) {
		return 0.0;
	}

	return std / std::abs(mean);
}

// ============================================================================
// Component fitting methods
// ============================================================================

std::vector<double> MFLES::fitMedianComponent(const std::vector<double>& data) {
	// Phase 8 Fix #1: Return vector filled with median value (matches statsforecast)
	// Compute median (global or moving window)
	const int n = static_cast<int>(data.size());
	double median_value;

	if (!params_.moving_medians) {
		// Global median (default)
		std::vector<double> sorted_data = data;
		std::sort(sorted_data.begin(), sorted_data.end());

		if (n % 2 == 0) {
			median_value = (sorted_data[n/2 - 1] + sorted_data[n/2]) / 2.0;
		} else {
			median_value = sorted_data[n/2];
		}
	} else {
		// Moving window median (use most recent seasonal cycle)
		// This provides a more adaptive baseline that focuses on recent data
		if (params_.seasonal_periods.empty()) {
			// No seasonality, fall back to global median
			std::vector<double> sorted_data = data;
			std::sort(sorted_data.begin(), sorted_data.end());
			median_value = (n % 2 == 0) ? (sorted_data[n/2 - 1] + sorted_data[n/2]) / 2.0 : sorted_data[n/2];
		} else {
			// Use the primary seasonal period to determine window size
			int period = params_.seasonal_periods[0];
			int window_size = std::min(2 * period, n);  // Use 2 cycles or all data
			int start_idx = std::max(0, n - window_size);

			// Compute median of recent window
			std::vector<double> window_data(data.begin() + start_idx, data.end());
			std::sort(window_data.begin(), window_data.end());

			int w = static_cast<int>(window_data.size());
			if (w % 2 == 0) {
				median_value = (window_data[w/2 - 1] + window_data[w/2]) / 2.0;
			} else {
				median_value = window_data[w/2];
			}
		}
	}

	// Return vector filled with the median value (like statsforecast's np.full_like)
	return std::vector<double>(n, median_value);
}

std::vector<double> MFLES::fitLinearTrend(const std::vector<double>& data) {
	// Simple OLS linear regression: y = mx + b
	const int n = static_cast<int>(data.size());
	std::vector<double> trend_fit(n);

	// Compute means
	double mean_x = (n - 1) / 2.0;
	double mean_y = std::accumulate(data.begin(), data.end(), 0.0) / n;

	// Compute slope
	double numerator = 0.0;
	double denominator = 0.0;

	for (int i = 0; i < n; ++i) {
		double x_dev = i - mean_x;
		numerator += x_dev * (data[i] - mean_y);
		denominator += x_dev * x_dev;
	}

	if (std::abs(denominator) < EPSILON) {
		// No trend (flat)
		trend_slope_ = 0.0;
		trend_intercept_ = mean_y;
	} else {
		trend_slope_ = numerator / denominator;
		trend_intercept_ = mean_y - trend_slope_ * mean_x;
	}

	// Compute fitted values
	for (int i = 0; i < n; ++i) {
		trend_fit[i] = trend_intercept_ + trend_slope_ * i;
	}

	return trend_fit;
}

std::vector<double> MFLES::fitSiegelTrend(const std::vector<double>& data) {
	// Siegel Repeated Medians robust regression
	// Highly resistant to outliers compared to OLS
	const int n = static_cast<int>(data.size());

	if (n < 2) {
		return std::vector<double>(n, 0.0);
	}

	// Create x vector (time indices: 0, 1, 2, ...)
	std::vector<double> x(n);
	std::iota(x.begin(), x.end(), 0.0);

	// Compute robust slope and intercept using Siegel regression
	double slope = 0.0;
	double intercept = 0.0;
	utils::RobustRegression::siegelRepeatedMedians(x, data, slope, intercept);

	// Store for forecasting
	trend_slope_ = slope;
	trend_intercept_ = intercept;

	// Generate fitted values
	std::vector<double> trend_fit(n);
	for (int i = 0; i < n; ++i) {
		trend_fit[i] = intercept + slope * i;
	}

	return trend_fit;
}

std::vector<double> MFLES::fitPiecewiseTrend(const std::vector<double>& data) {
	// Simplified piecewise linear trend
	// Places changepoints at evenly-spaced intervals and fits segments with OLS
	// TODO: Enhance with LASSO-based changepoint selection in future

	const int n = static_cast<int>(data.size());

	if (n < 4) {
		// Not enough data for piecewise, fall back to simple linear
		return fitLinearTrend(data);
	}

	// Determine number of changepoints
	int n_changepoints = static_cast<int>(n * params_.n_changepoints_pct);
	n_changepoints = std::max(1, std::min(n_changepoints, n / 2));  // At least 1, at most n/2

	if (!params_.changepoints || n_changepoints < 1) {
		// Changepoints disabled, use simple linear
		return fitLinearTrend(data);
	}

	// Place changepoints at evenly spaced intervals
	changepoint_indices_.clear();
	changepoint_indices_.reserve(n_changepoints);

	for (int i = 1; i <= n_changepoints; ++i) {
		int idx = (i * n) / (n_changepoints + 1);
		changepoint_indices_.push_back(idx);
	}

	// Fit piecewise segments
	std::vector<double> trend_fit(n, 0.0);
	std::vector<double> segment_slopes;
	std::vector<double> segment_intercepts;

	// Add boundaries
	std::vector<int> boundaries = {0};
	boundaries.insert(boundaries.end(), changepoint_indices_.begin(), changepoint_indices_.end());
	boundaries.push_back(n);

	// Fit each segment
	for (size_t seg = 0; seg < boundaries.size() - 1; ++seg) {
		int start = boundaries[seg];
		int end = boundaries[seg + 1];
		int seg_len = end - start;

		if (seg_len < 2) continue;

		// Extract segment data
		std::vector<double> seg_data(data.begin() + start, data.begin() + end);
		std::vector<double> seg_x(seg_len);
		std::iota(seg_x.begin(), seg_x.end(), 0.0);

		// Fit linear trend to segment using OLS
		double mean_x = std::accumulate(seg_x.begin(), seg_x.end(), 0.0) / seg_len;
		double mean_y = std::accumulate(seg_data.begin(), seg_data.end(), 0.0) / seg_len;

		double num = 0.0, den = 0.0;
		for (int i = 0; i < seg_len; ++i) {
			num += (seg_x[i] - mean_x) * (seg_data[i] - mean_y);
			den += (seg_x[i] - mean_x) * (seg_x[i] - mean_x);
		}

		double slope = (den > 1e-10) ? (num / den) : 0.0;
		double intercept = mean_y - slope * mean_x;

		segment_slopes.push_back(slope);
		segment_intercepts.push_back(intercept);

		// Apply to fitted values (using global indices)
		for (int i = start; i < end; ++i) {
			int local_i = i - start;
			trend_fit[i] = intercept + slope * local_i;
		}
	}

	// Store overall slope/intercept (use first segment as representative)
	if (!segment_slopes.empty()) {
		trend_slope_ = segment_slopes[0];
		trend_intercept_ = segment_intercepts[0];
	}

	// Store changepoint coefficients for forecasting
	changepoint_coefs_ = segment_slopes;

	return trend_fit;
}

std::vector<double> MFLES::fitFourierSeason(const std::vector<double>& data, int period, bool weighted) {
	// Fit Fourier series: sum of sin/cos pairs
	const int n = static_cast<int>(data.size());
	std::vector<double> seasonal_fit(n, 0.0);

	// Determine number of Fourier pairs
	int K = (params_.fourier_order > 0) ? params_.fourier_order : adaptiveK(period);
	K = std::min(K, period / 2);  // Cannot exceed period/2
	K = std::min(K, MAX_FOURIER_TERMS);

	if (K < 1) {
		return seasonal_fit;
	}

	// Get weights: either time-varying seasonality weights or uniform (1.0 for OLS)
	std::vector<double> weights;
	if (weighted) {
		weights = getSeasonalityWeights(n, period);
	} else {
		weights = std::vector<double>(n, 1.0);  // OLS: all weights = 1.0
	}

	// Build design matrix X for Fourier basis: [sin(2π*1*t/p), cos(2π*1*t/p), sin(2π*2*t/p), ...]
	// Design matrix has n rows and 2K columns
	std::vector<std::vector<double>> X(n, std::vector<double>(2 * K));

	for (int i = 0; i < n; ++i) {
		for (int k = 1; k <= K; ++k) {
			double angle = 2.0 * PI * k * i / period;
			X[i][2*(k-1)] = std::sin(angle);      // sin basis for frequency k
			X[i][2*(k-1)+1] = std::cos(angle);    // cos basis for frequency k
		}
	}

	// Solve WLS normal equations: (X'WX)β = X'Wy
	// For efficiency, we compute X'WX and X'Wy directly
	// When weights are all 1.0, this is mathematically equivalent to OLS

	// Compute X'WX (2K x 2K symmetric matrix)
	std::vector<std::vector<double>> XtWX(2*K, std::vector<double>(2*K, 0.0));
	for (int j = 0; j < 2*K; ++j) {
		for (int l = j; l < 2*K; ++l) {  // Only upper triangle (symmetric)
			double sum = 0.0;
			for (int i = 0; i < n; ++i) {
				sum += weights[i] * X[i][j] * X[i][l];
			}
			XtWX[j][l] = sum;
			XtWX[l][j] = sum;  // Mirror to lower triangle
		}
	}

	// Compute X'Wy (2K vector)
	std::vector<double> XtWy(2*K, 0.0);
	for (int j = 0; j < 2*K; ++j) {
		for (int i = 0; i < n; ++i) {
			XtWy[j] += weights[i] * X[i][j] * data[i];
		}
	}

	// Solve linear system XtWX * beta = XtWy using Gaussian elimination
	// For Fourier bases, XtWX should be positive definite
	std::vector<double> beta(2*K, 0.0);

	// Simple Gaussian elimination for small systems (2K typically < 30)
	// Make a copy for in-place modification
	auto A = XtWX;
	auto b = XtWy;

	// Forward elimination
	for (int k = 0; k < 2*K; ++k) {
		// Find pivot
		int pivot = k;
		for (int i = k + 1; i < 2*K; ++i) {
			if (std::abs(A[i][k]) > std::abs(A[pivot][k])) {
				pivot = i;
			}
		}

		// Swap rows
		if (pivot != k) {
			std::swap(A[k], A[pivot]);
			std::swap(b[k], b[pivot]);
		}

		// Skip if pivot is too small
		if (std::abs(A[k][k]) < EPSILON) {
			continue;
		}

		// Eliminate
		for (int i = k + 1; i < 2*K; ++i) {
			double factor = A[i][k] / A[k][k];
			for (int j = k; j < 2*K; ++j) {
				A[i][j] -= factor * A[k][j];
			}
			b[i] -= factor * b[k];
		}
	}

	// Back substitution
	for (int i = 2*K - 1; i >= 0; --i) {
		if (std::abs(A[i][i]) < EPSILON) {
			beta[i] = 0.0;
			continue;
		}
		beta[i] = b[i];
		for (int j = i + 1; j < 2*K; ++j) {
			beta[i] -= A[i][j] * beta[j];
		}
		beta[i] /= A[i][i];
	}

	// Extract sin and cos coefficients from beta
	std::vector<double> sin_coeffs(K);
	std::vector<double> cos_coeffs(K);
	for (int k = 0; k < K; ++k) {
		sin_coeffs[k] = beta[2*k];
		cos_coeffs[k] = beta[2*k + 1];
	}

	// Store coefficients for forecasting
	fourier_coeffs_[period] = {sin_coeffs, cos_coeffs, K};

	// Compute fitted seasonal values
	for (int i = 0; i < n; ++i) {
		for (int k = 1; k <= K; ++k) {
			double angle = 2.0 * PI * k * i / period;
			seasonal_fit[i] += sin_coeffs[k-1] * std::sin(angle) + cos_coeffs[k-1] * std::cos(angle);
		}
	}

	return seasonal_fit;
}

std::vector<double> MFLES::fitESEnsemble(const std::vector<double>& data) {
	// ES Ensemble: average forecasts from multiple alpha values
	// This reduces sensitivity to alpha selection by averaging over a range

	const int n = static_cast<int>(data.size());

	// Generate alpha values evenly spaced between min_alpha and max_alpha
	es_ensemble_alphas_.clear();
	int num_steps = std::max(1, params_.es_ensemble_steps);

	if (num_steps == 1) {
		// Single alpha: use average
		es_ensemble_alphas_.push_back((params_.min_alpha + params_.max_alpha) / 2.0);
	} else {
		// Multiple alphas: evenly spaced
		for (int i = 0; i < num_steps; ++i) {
			double alpha = params_.min_alpha + (params_.max_alpha - params_.min_alpha) * i / (num_steps - 1);
			es_ensemble_alphas_.push_back(alpha);
		}
	}

	// Initialize ensemble fit (will be averaged across all alphas)
	std::vector<double> es_fit(n, 0.0);

	// Storage for final levels from each alpha (for forecasting)
	std::vector<double> final_levels;
	final_levels.reserve(es_ensemble_alphas_.size());

	// Run ES for each alpha value
	for (double alpha : es_ensemble_alphas_) {
		// Initialize level with first value (matches statsforecast)
		double level = data[0];

		// Fit ES with this alpha
		for (int i = 0; i < n; ++i) {
			// Store one-step-ahead forecast
			es_fit[i] += level;

			// Update level
			level = alpha * data[i] + (1.0 - alpha) * level;
		}

		// Store final level for this alpha
		final_levels.push_back(level);
	}

	// Average the fitted values across all alphas
	for (int i = 0; i < n; ++i) {
		es_fit[i] /= es_ensemble_alphas_.size();
	}

	// Average final levels for forecasting
	final_level_ = std::accumulate(final_levels.begin(), final_levels.end(), 0.0) / final_levels.size();

	return es_fit;
}

std::vector<double> MFLES::fitMovingAverage(const std::vector<double>& data, int window) {
	// Simple moving average
	const int n = static_cast<int>(data.size());
	std::vector<double> ma_fit(n);

	window = std::min(window, n);

	for (int i = 0; i < n; ++i) {
		int start = std::max(0, i - window + 1);
		int count = i - start + 1;

		double sum = 0.0;
		for (int j = start; j <= i; ++j) {
			sum += data[j];
		}

		ma_fit[i] = sum / count;
	}

	// Final level is last MA value
	final_level_ = ma_fit[n-1];

	return ma_fit;
}

// ============================================================================
// Outlier handling
// ============================================================================

void MFLES::capOutliers(std::vector<double>& data) const {
	// Cap extreme values at mean ± N*sigma
	const int n = static_cast<int>(data.size());

	double mean = std::accumulate(data.begin(), data.end(), 0.0) / n;

	double variance = 0.0;
	for (const auto& val : data) {
		variance += (val - mean) * (val - mean);
	}
	double std = std::sqrt(variance / n);

	double lower_bound = mean - params_.outlier_sigma * std;
	double upper_bound = mean + params_.outlier_sigma * std;

	for (auto& val : data) {
		if (val < lower_bound) val = lower_bound;
		if (val > upper_bound) val = upper_bound;
	}
}

std::vector<bool> MFLES::detectOutliers(const std::vector<double>& data) const {
	// Detect outliers using mean ± N*sigma threshold
	const int n = static_cast<int>(data.size());
	std::vector<bool> is_outlier(n, false);

	double mean = std::accumulate(data.begin(), data.end(), 0.0) / n;

	double variance = 0.0;
	for (const auto& val : data) {
		variance += (val - mean) * (val - mean);
	}
	double std = std::sqrt(variance / n);

	double lower_bound = mean - params_.outlier_sigma * std;
	double upper_bound = mean + params_.outlier_sigma * std;

	for (int i = 0; i < n; ++i) {
		is_outlier[i] = (data[i] < lower_bound || data[i] > upper_bound);
	}

	return is_outlier;
}

// ============================================================================
// Fourier helpers
// ============================================================================

int MFLES::optimalK(int period) const {
	// Simple formula: min(period/2, MAX_FOURIER_TERMS)
	return std::min(period / 2, MAX_FOURIER_TERMS);
}

int MFLES::adaptiveK(int period) const {
	// Match statsforecast adaptive logic:
	// period < 10 → K=5
	// period < 70 → K=10
	// period >= 70 → K=15 (capped by MAX_FOURIER_TERMS)

	if (period < 10) {
		return std::min(5, period / 2);
	} else if (period < 70) {
		return std::min(10, period / 2);
	} else {
		return std::min(15, std::min(period / 2, MAX_FOURIER_TERMS));
	}
}

std::vector<double> MFLES::getSeasonalityWeights(int n, int period) const {
	// Time-varying weights: increasing importance over time
	// Weight = 1 + (cycle_number / total_cycles)
	std::vector<double> weights(n);

	for (int i = 0; i < n; ++i) {
		double cycle_num = static_cast<double>(i) / period;
		double total_cycles = static_cast<double>(n) / period;
		weights[i] = 1.0 + (cycle_num / total_cycles);
	}

	return weights;
}

// ============================================================================
// Forecasting methods
// ============================================================================

core::Forecast MFLES::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("MFLES: must call fit() before predict()");
	}

	const int n = static_cast<int>(preprocessed_data_.size());
	std::vector<double> forecast(horizon, 0.0);

	// Add median component (Phase 8 Fix #1: Use last value from vector, since all values are the same)
	if (!median_component_.empty()) {
		double median_value = median_component_.back();  // All values are the same, so use last
		for (int h = 0; h < horizon; ++h) {
			forecast[h] += median_value;
		}
	}

	// Add trend projection
	auto trend_proj = projectTrend(horizon, n);
	for (int h = 0; h < horizon; ++h) {
		forecast[h] += trend_proj[h];
	}

	// Add seasonal projection (cyclic)
	for (const auto& [period, coeffs] : fourier_coeffs_) {
		auto seasonal_proj = projectFourier(period, horizon, n);
		for (int h = 0; h < horizon; ++h) {
			forecast[h] += seasonal_proj[h];
		}
	}

	// Add level (constant)
	auto level_proj = projectLevel(horizon);
	for (int h = 0; h < horizon; ++h) {
		forecast[h] += level_proj[h];
	}

	// Reverse preprocessing transformations
	postprocess(forecast);

	// TODO: Compute prediction intervals (Phase 8)
	// For now, return empty intervals (will implement in Phase 8)

	// Wrap forecast in Matrix format (vector of vectors for multidimensional support)
	return core::Forecast{{forecast}, std::nullopt, std::nullopt};
}

std::vector<double> MFLES::projectTrend(int horizon, int start_index) const {
	// Project linear trend with optional penalty
	std::vector<double> projection(horizon);

	// If we have accumulated trend values (last 2 fitted values), use them
	if (accumulated_trend_.size() >= 2) {
		// Compute slope from last 2 fitted values
		double slope = accumulated_trend_[1] - accumulated_trend_[0];
		double last_point = accumulated_trend_[1];

		double penalty = 1.0;
		if (params_.trend_penalty) {
			penalty = computeTrendPenalty();
		}

		// Project forward: y[t] = last_point + slope * (h+1)
		for (int h = 0; h < horizon; ++h) {
			projection[h] = (slope * (h + 1) + last_point) * penalty;
		}
	} else {
		// Fallback: no trend (should not happen in normal use)
		for (int h = 0; h < horizon; ++h) {
			projection[h] = 0.0;
		}
	}

	return projection;
}

std::vector<double> MFLES::projectFourier(int period, int horizon, int start_index) const {
	// Project Fourier seasonality (cyclic)
	std::vector<double> projection(horizon, 0.0);

	auto it = fourier_coeffs_.find(period);
	if (it == fourier_coeffs_.end()) {
		return projection;
	}

	const auto& coeffs = it->second;
	const int K = coeffs.K;

	for (int h = 0; h < horizon; ++h) {
		int t = start_index + h;

		for (int k = 1; k <= K; ++k) {
			double angle = 2.0 * PI * k * t / period;
			projection[h] += coeffs.sin_coeffs[k-1] * std::sin(angle);
			projection[h] += coeffs.cos_coeffs[k-1] * std::cos(angle);
		}
	}

	return projection;
}

std::vector<double> MFLES::projectLevel(int horizon) const {
	// Constant level projection
	return std::vector<double>(horizon, final_level_);
}

double MFLES::computeTrendPenalty() const {
	// R²-based penalty for trend extrapolation
	// penalty = max(0, R²)

	double r_squared = computeRSquared(preprocessed_data_, trend_component_);
	return std::max(0.0, r_squared);
}

// ============================================================================
// Diagnostic methods
// ============================================================================

void MFLES::computeFittedValues() {
	const int n = static_cast<int>(preprocessed_data_.size());
	fitted_ = std::vector<double>(n, 0.0);

	// Sum all components
	for (int i = 0; i < n; ++i) {
		// Phase 8 Fix #1: Handle median as vector
		if (!median_component_.empty()) {
			fitted_[i] += median_component_[i];
		}
		fitted_[i] += trend_component_[i];

		for (const auto& [period, seasonal] : seasonal_components_) {
			fitted_[i] += seasonal[i];
		}

		fitted_[i] += level_component_[i];
	}

	// Reverse preprocessing for fitted values
	auto fitted_original = fitted_;
	postprocess(fitted_original);

	// Compute residuals on original scale
	residuals_ = std::vector<double>(n);
	for (int i = 0; i < n; ++i) {
		residuals_[i] = original_data_[i] - fitted_original[i];
	}
}

double MFLES::computeRSquared(const std::vector<double>& actual, const std::vector<double>& fitted) const {
	const int n = static_cast<int>(actual.size());

	double mean = std::accumulate(actual.begin(), actual.end(), 0.0) / n;

	double ss_tot = 0.0;
	double ss_res = 0.0;

	for (int i = 0; i < n; ++i) {
		ss_tot += (actual[i] - mean) * (actual[i] - mean);
		ss_res += (actual[i] - fitted[i]) * (actual[i] - fitted[i]);
	}

	if (ss_tot < EPSILON) {
		return 0.0;
	}

	return 1.0 - (ss_res / ss_tot);
}

MFLES::Decomposition MFLES::seasonal_decompose() const {
	if (!is_fitted_) {
		throw std::runtime_error("MFLES: must call fit() before seasonal_decompose()");
	}

	Decomposition decomp;

	// Convert components back to original scale
	decomp.trend = trend_component_;
	postprocess(decomp.trend);

	// Aggregate all seasonal components
	const int n = static_cast<int>(trend_component_.size());
	decomp.seasonal = std::vector<double>(n, 0.0);

	for (const auto& [period, seasonal] : seasonal_components_) {
		for (int i = 0; i < n; ++i) {
			decomp.seasonal[i] += seasonal[i];
		}
	}
	postprocess(decomp.seasonal);

	decomp.level = level_component_;
	postprocess(decomp.level);

	decomp.residuals = residuals_;

	return decomp;
}

} // namespace anofoxtime::models
