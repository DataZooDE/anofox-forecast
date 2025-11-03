#include "anofox-time/models/auto_arima.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/models/arima.hpp"
#include "anofox-time/seasonality/stl.hpp"
#include "anofox-time/utils/logging.hpp"

#include <Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {

// Helper function to compute AICc from AIC
double computeAICc(double aic, int k, std::size_t n) {
	if (n <= static_cast<std::size_t>(k) + 1) {
		return std::numeric_limits<double>::infinity();
	}
	return aic + (2.0 * static_cast<double>(k) * static_cast<double>(k + 1)) /
	             static_cast<double>(n - k - 1);
}

// Helper function to compute BIC from AIC
// BIC = AIC + k * (log(nstar) - 2)
// where nstar is the effective sample size after differencing
double computeBIC(double aic, int k, std::size_t nstar) {
	return aic + static_cast<double>(k) * (std::log(static_cast<double>(nstar)) - 2.0);
}

// Count parameters in ARIMA model
int countParameters(int p, int q, int P, int Q, bool has_drift, bool has_constant) {
	int k = p + q + P + Q;
	if (has_drift) k++;
	if (has_constant) k++;
	return k;
}

// Check if polynomial roots are outside unit circle (admissibility check)
// For AR polynomial: roots of (1 - φ₁z - φ₂z² - ...) should have |z| > 1.01
// For MA polynomial: roots of (1 + θ₁z + θ₂z² + ...) should have |z| > 1.01
// Uses companion matrix method: eigenvalues are reciprocals of roots
bool checkPolynomialRoots(const Eigen::VectorXd &coeffs, double tolerance = 1.01) {
	if (coeffs.size() == 0) {
		return true; // No coefficients, trivially admissible
	}

	// Filter out negligible coefficients (< 1e-8)
	std::vector<double> significant_coeffs;
	for (int i = 0; i < coeffs.size(); ++i) {
		if (std::abs(coeffs(i)) > 1e-8) {
			significant_coeffs.push_back(coeffs(i));
		}
	}

	if (significant_coeffs.empty()) {
		return true; // No significant coefficients
	}

	int p = static_cast<int>(significant_coeffs.size());

	// Build companion matrix
	// For AR polynomial (1 - φ₁z - φ₂z² - ...):
	// Companion matrix is:
	// [ φ₁  φ₂  φ₃  ...  φₚ ]
	// [  1   0   0  ...   0 ]
	// [  0   1   0  ...   0 ]
	// [ ...                 ]
	// [  0   0   0   1   0 ]
	Eigen::MatrixXd companion = Eigen::MatrixXd::Zero(p, p);

	// First row contains coefficients
	for (int i = 0; i < p; ++i) {
		companion(0, i) = significant_coeffs[i];
	}

	// Sub-diagonal is identity
	for (int i = 1; i < p; ++i) {
		companion(i, i - 1) = 1.0;
	}

	// Compute eigenvalues
	Eigen::EigenSolver<Eigen::MatrixXd> solver(companion);
	if (solver.info() != Eigen::Success) {
		// Eigenvalue computation failed, reject model
		return false;
	}

	// Check if all eigenvalues have magnitude < 1/tolerance
	// (eigenvalues are reciprocals of roots, so |eigenvalue| < 1/tolerance means |root| > tolerance)
	double max_eigenvalue_abs = 0.0;
	for (int i = 0; i < solver.eigenvalues().size(); ++i) {
		double abs_eigenvalue = std::abs(solver.eigenvalues()(i));
		max_eigenvalue_abs = std::max(max_eigenvalue_abs, abs_eigenvalue);
	}

	// Model is admissible if max eigenvalue magnitude < 1/tolerance (i.e., min root > tolerance)
	return max_eigenvalue_abs < (1.0 / tolerance);
}

// Check if ARIMA model is admissible (AR and MA polynomials have roots outside unit circle)
bool checkModelAdmissibility(const std::unique_ptr<anofoxtime::models::ARIMA> &model, double tolerance = 1.01) {
	// Check AR polynomial (non-seasonal)
	const auto &ar_coeffs = model->arCoefficients();
	if (!checkPolynomialRoots(ar_coeffs, tolerance)) {
		return false;
	}

	// Check MA polynomial (non-seasonal)
	const auto &ma_coeffs = model->maCoefficients();
	if (!checkPolynomialRoots(ma_coeffs, tolerance)) {
		return false;
	}

	// Check seasonal AR polynomial
	const auto &sar_coeffs = model->seasonalARCoefficients();
	if (!checkPolynomialRoots(sar_coeffs, tolerance)) {
		return false;
	}

	// Check seasonal MA polynomial
	const auto &sma_coeffs = model->seasonalMACoefficients();
	if (!checkPolynomialRoots(sma_coeffs, tolerance)) {
		return false;
	}

	return true;
}

// Compute KPSS test statistic for level stationarity
// Returns the KPSS statistic (higher values indicate non-stationarity)
double computeKPSS(const std::vector<double> &data) {
	std::size_t n = data.size();
	if (n < 10) {
		return 0.0;
	}

	// 1. Compute mean
	double mean = std::accumulate(data.begin(), data.end(), 0.0) / static_cast<double>(n);

	// 2. Compute residuals (deviations from mean for level stationary test)
	std::vector<double> residuals;
	residuals.reserve(n);
	for (double val : data) {
		residuals.push_back(val - mean);
	}

	// 3. Compute partial sums (cumulative sum of residuals)
	std::vector<double> partial_sums;
	partial_sums.reserve(n);
	double cumsum = 0.0;
	for (double resid : residuals) {
		cumsum += resid;
		partial_sums.push_back(cumsum);
	}

	// 4. Compute sum of squared partial sums
	double sum_sq_partial = 0.0;
	for (double s : partial_sums) {
		sum_sq_partial += s * s;
	}

	// 5. Estimate long-run variance using Newey-West with automatic lag selection
	// Use bandwidth = floor(4 * (n/100)^(1/4)) as suggested by Schwert (1989)
	int bandwidth = static_cast<int>(std::floor(4.0 * std::pow(static_cast<double>(n) / 100.0, 0.25)));
	bandwidth = std::max(1, std::min(bandwidth, static_cast<int>(n) / 4));

	// Short-run variance (lag 0)
	double variance = 0.0;
	for (double resid : residuals) {
		variance += resid * resid;
	}
	variance /= static_cast<double>(n);

	// Add autocovariances with Bartlett kernel weights
	double long_run_var = variance;
	for (int lag = 1; lag <= bandwidth; ++lag) {
		double autocovariance = 0.0;
		for (std::size_t t = lag; t < n; ++t) {
			autocovariance += residuals[t] * residuals[t - lag];
		}
		autocovariance /= static_cast<double>(n);

		// Bartlett kernel weight: 1 - lag/(bandwidth+1)
		double weight = 1.0 - static_cast<double>(lag) / static_cast<double>(bandwidth + 1);
		long_run_var += 2.0 * weight * autocovariance;
	}

	// Ensure positive variance
	if (long_run_var <= 0.0) {
		long_run_var = variance;
	}

	// 6. Compute KPSS test statistic
	double n_sq = static_cast<double>(n) * static_cast<double>(n);
	double kpss_stat = sum_sq_partial / (n_sq * long_run_var);

	return kpss_stat;
}

// KPSS-based differencing test
// Returns suggested number of differences (0, 1, or 2)
// Uses KPSS test with critical value at 5% significance level
int ndiffsTest(const std::vector<double> &data, int max_d) {
	if (data.size() < 10 || max_d == 0) {
		return 0;
	}

	// KPSS critical value at 5% significance level for level stationary test
	const double kpss_critical = 0.463;

	// Test original series
	double kpss_stat = computeKPSS(data);

	// If stationary (KPSS stat < critical value), no differencing needed
	if (kpss_stat < kpss_critical) {
		return 0;
	}

	// Series is non-stationary, try first differencing
	if (max_d < 1) {
		return 0;
	}

	std::vector<double> diff1;
	diff1.reserve(data.size() - 1);
	for (std::size_t i = 1; i < data.size(); ++i) {
		diff1.push_back(data[i] - data[i - 1]);
	}

	double kpss_stat_d1 = computeKPSS(diff1);

	// If first difference is stationary, return d=1
	if (kpss_stat_d1 < kpss_critical) {
		return 1;
	}

	// First difference still non-stationary, try second differencing
	if (max_d < 2) {
		return 1;  // Return 1 even if not stationary (limit reached)
	}

	std::vector<double> diff2;
	diff2.reserve(diff1.size() - 1);
	for (std::size_t i = 1; i < diff1.size(); ++i) {
		diff2.push_back(diff1[i] - diff1[i - 1]);
	}

	double kpss_stat_d2 = computeKPSS(diff2);

	// If second difference is stationary, return d=2
	if (kpss_stat_d2 < kpss_critical) {
		return 2;
	}

	// Even d=2 not stationary, but return 2 (limit reached)
	return 2;
}

// Simple seasonal differencing test
int nsdiffsTest(const std::vector<double> &data, int seasonal_period, int max_D) {
	if (data.size() < static_cast<std::size_t>(seasonal_period * 2) || seasonal_period <= 1 || max_D == 0) {
		return 0;
	}

	// Compute seasonal variance
	std::vector<double> seasonal_diffs;
	seasonal_diffs.reserve(data.size() - static_cast<std::size_t>(seasonal_period));
	for (std::size_t i = static_cast<std::size_t>(seasonal_period); i < data.size(); ++i) {
		seasonal_diffs.push_back(data[i] - data[i - static_cast<std::size_t>(seasonal_period)]);
	}

	double mean_sd = std::accumulate(seasonal_diffs.begin(), seasonal_diffs.end(), 0.0) / 
	                 static_cast<double>(seasonal_diffs.size());
	double variance_sd = 0.0;
	for (double val : seasonal_diffs) {
		double diff = val - mean_sd;
		variance_sd += diff * diff;
	}
	variance_sd /= static_cast<double>(seasonal_diffs.size());

	// Compare with original variance
	double mean = std::accumulate(data.begin(), data.end(), 0.0) / static_cast<double>(data.size());
	double variance = 0.0;
	for (double val : data) {
		double diff = val - mean;
		variance += diff * diff;
	}
	variance /= static_cast<double>(data.size());

	// If seasonal differencing reduces variance significantly, suggest D=1
	if (variance_sd < variance * 0.7 && max_D >= 1) {
		return 1;
	}

	return 0;
}

// STL-based seasonal differencing test (matching R's nsdiffs approach)
// Uses STL decomposition to compute seasonal strength
// Returns 1 if seasonal_strength > 0.64, otherwise 0
int nsdiffsSTL(const std::vector<double> &data, int seasonal_period, int max_D) {
	if (data.size() < static_cast<std::size_t>(seasonal_period * 2) || seasonal_period <= 1 || max_D == 0) {
		return 0;
	}

	try {
		// Create time series for STL decomposition
		std::vector<anofoxtime::core::TimeSeries::TimePoint> timestamps(data.size());
		auto start = anofoxtime::core::TimeSeries::TimePoint{};
		for (std::size_t i = 0; i < data.size(); ++i) {
			timestamps[i] = start + std::chrono::seconds(static_cast<long>(i));
		}
		anofoxtime::core::TimeSeries ts(std::move(timestamps), data);

		// Perform STL decomposition with appropriate parameters
		// Use robust=false to match R's default nsdiffs behavior
		auto stl = anofoxtime::seasonality::STLDecomposition::builder()
			.withPeriod(static_cast<std::size_t>(seasonal_period))
			.withIterations(2)
			.withRobust(false)
			.build();

		stl.fit(ts);

		// Get seasonal strength from STL decomposition
		// seasonal_strength = 1 - var(remainder) / var(seasonal + remainder)
		double seasonal_strength = stl.seasonalStrength();

		// R's nsdiffs threshold: seasonal_strength > 0.64 → need D=1
		// This is the default threshold in forecast::nsdiffs()
		if (seasonal_strength > 0.64) {
			return 1;
		}

		return 0;
	} catch (const std::exception &e) {
		// If STL fails (e.g., series too short or problematic), fallback to no seasonal differencing
		// This is safer than failing completely
		return 0;
	}
}

bool betterIC(double ic1, double ic2) {
	if (!std::isfinite(ic1)) return false;
	if (!std::isfinite(ic2)) return true;
	return ic1 < ic2;
}

} // namespace

namespace anofoxtime::models {

class AutoARIMA::CandidateEvaluator {
public:
	CandidateEvaluator(const AutoARIMA &owner, const std::vector<double> &data);

	CandidateResult evaluate(const CandidateConfig &config) const;

private:
	const AutoARIMA &owner_;
	const std::vector<double> &data_;
	std::size_t n_;
};

AutoARIMA::CandidateEvaluator::CandidateEvaluator(const AutoARIMA &owner,
                                                  const std::vector<double> &data)
    : owner_(owner), data_(data), n_(data.size()) {}

AutoARIMA::CandidateResult AutoARIMA::CandidateEvaluator::evaluate(const CandidateConfig &config) const {
	CandidateResult result;
	result.config = config;

	try {
		// Build SARIMA model using the extended ARIMA class with full seasonal support
		auto model = ARIMABuilder()
		                 .withAR(config.p)
		                 .withDifferencing(config.d)
		                 .withMA(config.q)
		                 .withSeasonalAR(config.P)
		                 .withSeasonalDifferencing(config.D)
		                 .withSeasonalMA(config.Q)
		                 .withSeasonalPeriod(owner_.seasonal_period_)
		                 .withIntercept(config.include_constant || config.include_drift)
		                 .build();

		// Create a temporary TimeSeries for fitting
		// Generate simple timestamps (one per data point)
		std::vector<core::TimeSeries::TimePoint> timestamps;
		timestamps.reserve(data_.size());
		auto start = core::TimeSeries::TimePoint{};
		for (std::size_t i = 0; i < data_.size(); ++i) {
			timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
		}
		core::TimeSeries ts(std::move(timestamps), data_);
		model->fit(ts);

		// Check model admissibility (AR/MA polynomial roots must be outside unit circle)
		// Following R's auto.arima approach: reject models with roots < 1.01 in magnitude
		if (!checkModelAdmissibility(model, 1.01)) {
			// Model is inadmissible (unstable or non-invertible), mark as invalid
			result.valid = false;
			result.model = nullptr;
			return result;
		}

		// Extract metrics
		result.metrics.log_likelihood = std::numeric_limits<double>::quiet_NaN();

		// Get AIC from model
		if (model->aic().has_value()) {
			result.metrics.aic = model->aic().value();
		}

		// Compute effective sample size (nstar) after differencing
		// nstar = n - d - D*seasonal_period (observations lost to differencing)
		std::size_t nstar = n_ - config.d - (config.D * owner_.seasonal_period_);

		// Count parameters
		int k = countParameters(config.p, config.q, config.P, config.Q,
		                       config.include_drift, config.include_constant);

		// Compute AICc using effective sample size
		if (std::isfinite(result.metrics.aic)) {
			// For very small samples, use AIC instead of AICc (R's behavior)
			if (nstar < 4) {
				result.metrics.aicc = result.metrics.aic;
			} else {
				result.metrics.aicc = computeAICc(result.metrics.aic, k, nstar);
			}

			// Compute BIC using effective sample size
			result.metrics.bic = computeBIC(result.metrics.aic, k, nstar);
		}

		// Compute sigma2 from residuals
		const auto &residuals = model->residuals();
		if (!residuals.empty()) {
			double sum_sq = 0.0;
			for (double r : residuals) {
				sum_sq += r * r;
			}
			result.metrics.sigma2 = sum_sq / static_cast<double>(residuals.size());
		}

		// Check if metrics are valid
		double ic_value;
		switch (owner_.ic_) {
		case InformationCriterion::AIC:
			ic_value = result.metrics.aic;
			break;
		case InformationCriterion::AICc:
			ic_value = result.metrics.aicc;
			break;
		case InformationCriterion::BIC:
			ic_value = result.metrics.bic;
			break;
		default:
			ic_value = result.metrics.aicc;
		}

		if (std::isfinite(ic_value)) {
			result.valid = true;
			result.model = std::move(model);
		}

	} catch (const std::exception &ex) {
		ANOFOX_DEBUG("AutoARIMA: Failed to fit ARIMA({},{},{}): {}", 
		             config.p, config.d, config.q, ex.what());
	}

	return result;
}

AutoARIMA::AutoARIMA(int seasonal_period) : seasonal_period_(seasonal_period) {
	if (seasonal_period_ < 0) {
		throw std::invalid_argument("AutoARIMA seasonal period must be non-negative.");
	}
}

void AutoARIMA::ensureUnivariate(const core::TimeSeries &ts) const {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("AutoARIMA currently supports univariate series only.");
	}
}

int AutoARIMA::determineDifferencing(const std::vector<double> &data, int max_d) const {
	return ndiffsTest(data, max_d);
}

int AutoARIMA::determineSeasonalDifferencing(const std::vector<double> &data,
                                             int seasonal_period, int max_D) const {
	if (seasonal_period <= 1 || !test_seasonal_) {
		return 0;
	}
	return nsdiffsSTL(data, seasonal_period, max_D);
}

std::vector<AutoARIMA::CandidateConfig> AutoARIMA::generateStepwiseCandidates(int d, int D) const {
	std::vector<CandidateConfig> candidates;

	// Baseline models for stepwise search (matching R's auto.arima)
	// Model 1: ARIMA(0,d,0) - white noise after differencing
	candidates.push_back(CandidateConfig{0, d, 0, 0, D, 0, false, d == 0});

	// Model 2: ARIMA(1,d,0) - simple AR(1) model
	if (max_p_ >= 1) {
		candidates.push_back(CandidateConfig{1, d, 0, 0, D, 0, false, d == 0});
	}

	// Model 3: ARIMA(0,d,1) - simple MA(1) model
	if (max_q_ >= 1) {
		candidates.push_back(CandidateConfig{0, d, 1, 0, D, 0, false, d == 0});
	}

	// Model 4: ARIMA(2,d,2) - R's default baseline
	if (start_p_ <= max_p_ && start_q_ <= max_q_) {
		candidates.push_back(CandidateConfig{start_p_, d, start_q_, 0, D, 0, false, d == 0});
	}

	// Model 5: ARIMA(0,d,0) with drift (only if d+D == 1, matching R's constraint)
	if ((d + D) == 1 && allow_drift_) {
		candidates.push_back(CandidateConfig{0, d, 0, 0, D, 0, true, false});
	}

	// Model 6: Grid search around (start_p, start_q)
	for (int p = std::max(0, start_p_ - 1); p <= std::min(max_p_, start_p_ + 1); ++p) {
		for (int q = std::max(0, start_q_ - 1); q <= std::min(max_q_, start_q_ + 1); ++q) {
			// Skip models already added
			if ((p == 0 && q == 0) ||  // Model 1
			    (p == 1 && q == 0) ||  // Model 2
			    (p == 0 && q == 1) ||  // Model 3
			    (p == start_p_ && q == start_q_)) {  // Model 4
				continue;
			}
			candidates.push_back(CandidateConfig{p, d, q, 0, D, 0, false, d == 0});
		}
	}

	// Add seasonal models if seasonal_period > 1
	if (seasonal_period_ > 1 && (start_P_ > 0 || start_Q_ > 0)) {
		// ARIMA(start_p,d,start_q)(start_P,D,start_Q)[s]
		if (start_P_ <= max_P_ && start_Q_ <= max_Q_) {
			candidates.push_back(CandidateConfig{start_p_, d, start_q_, start_P_, D, start_Q_, false, d == 0});
		}
	}

	return candidates;
}

std::vector<AutoARIMA::CandidateConfig> AutoARIMA::generateExhaustiveCandidates(int d, int D) const {
	std::vector<CandidateConfig> candidates;

	// Generate all combinations within bounds
	for (int p = 0; p <= max_p_; ++p) {
		for (int q = 0; q <= max_q_; ++q) {
			if (p == 0 && q == 0 && d == 0) {
				continue;  // Skip completely empty model
			}

			// Non-seasonal models
			candidates.push_back(CandidateConfig{p, d, q, 0, D, 0, false, d == 0 && p + q > 0});
			if (d > 0 && allow_drift_) {
				candidates.push_back(CandidateConfig{p, d, q, 0, D, 0, true, false});
			}

			// Seasonal models
			if (seasonal_period_ > 1) {
				for (int P = 0; P <= max_P_; ++P) {
					for (int Q = 0; Q <= max_Q_; ++Q) {
						if (P == 0 && Q == 0) continue;  // Already covered above
						candidates.push_back(CandidateConfig{p, d, q, P, D, Q, false, d == 0});
					}
				}
			}
		}
	}

	return candidates;
}

AutoARIMA::CandidateConfig AutoARIMA::findBestNeighbor(const CandidateConfig &current,
                                                       const std::vector<double> &data,
                                                       const CandidateEvaluator &evaluator) const {
	CandidateConfig best_neighbor = current;
	double best_ic = std::numeric_limits<double>::infinity();

	// Generate neighbors by varying p and q by ±1
	std::vector<CandidateConfig> neighbors;
	
	// Vary p
	if (current.p > 0) {
		auto config = current;
		config.p--;
		neighbors.push_back(config);
	}
	if (current.p < max_p_) {
		auto config = current;
		config.p++;
		neighbors.push_back(config);
	}

	// Vary q
	if (current.q > 0) {
		auto config = current;
		config.q--;
		neighbors.push_back(config);
	}
	if (current.q < max_q_) {
		auto config = current;
		config.q++;
		neighbors.push_back(config);
	}

	// Vary P and Q if seasonal
	if (seasonal_period_ > 1) {
		if (current.P > 0) {
			auto config = current;
			config.P--;
			neighbors.push_back(config);
		}
		if (current.P < max_P_) {
			auto config = current;
			config.P++;
			neighbors.push_back(config);
		}
		if (current.Q > 0) {
			auto config = current;
			config.Q--;
			neighbors.push_back(config);
		}
		if (current.Q < max_Q_) {
			auto config = current;
			config.Q++;
			neighbors.push_back(config);
		}
	}

	// Evaluate all neighbors
	for (const auto &neighbor : neighbors) {
		auto result = evaluator.evaluate(neighbor);
		if (!result.valid) continue;

		double ic;
		switch (ic_) {
		case InformationCriterion::AIC:
			ic = result.metrics.aic;
			break;
		case InformationCriterion::AICc:
			ic = result.metrics.aicc;
			break;
		case InformationCriterion::BIC:
			ic = result.metrics.bic;
			break;
		default:
			ic = result.metrics.aicc;
		}

		if (betterIC(ic, best_ic)) {
			best_ic = ic;
			best_neighbor = neighbor;
		}
	}

	return best_neighbor;
}

void AutoARIMA::fit(const core::TimeSeries &ts) {
	ensureUnivariate(ts);
	const auto &data = ts.getValues();
	std::size_t n = data.size();

	if (n < 10) {
		throw std::invalid_argument("AutoARIMA requires at least 10 observations.");
	}

	diagnostics_ = AutoARIMADiagnostics{};
	diagnostics_.training_data_size = n;
	diagnostics_.stepwise_used = stepwise_;

	// Adjust max parameters based on series length (R's approach)
	// This prevents overfitting on short series
	int orig_max_p = max_p_;
	int orig_max_q = max_q_;
	int orig_max_P = max_P_;
	int orig_max_Q = max_Q_;

	max_p_ = std::min(max_p_, static_cast<int>(n / 3));
	max_q_ = std::min(max_q_, static_cast<int>(n / 3));

	if (seasonal_period_ > 1) {
		max_P_ = std::min(max_P_, static_cast<int>(n / (3 * seasonal_period_)));
		max_Q_ = std::min(max_Q_, static_cast<int>(n / (3 * seasonal_period_)));
	}

	// Step 1: Determine differencing orders
	int d = determineDifferencing(data, max_d_);
	int D = (max_D_ > 0) ? determineSeasonalDifferencing(data, seasonal_period_, max_D_) : 0;

	ANOFOX_INFO("AutoARIMA: Selected differencing orders: d={}, D={}", d, D);

	// Step 2: Generate candidate models
	std::vector<CandidateConfig> initial_candidates;
	if (stepwise_) {
		initial_candidates = generateStepwiseCandidates(d, D);
	} else {
		initial_candidates = generateExhaustiveCandidates(d, D);
	}

	CandidateEvaluator evaluator(*this, data);

	// Step 3: Evaluate candidates
	CandidateResult best_result;
	double best_ic = std::numeric_limits<double>::infinity();

	for (const auto &candidate : initial_candidates) {
		auto result = evaluator.evaluate(candidate);
		diagnostics_.models_evaluated++;
		
		if (!result.valid) {
			diagnostics_.models_failed++;
			continue;
		}

		double ic;
		switch (ic_) {
		case InformationCriterion::AIC:
			ic = result.metrics.aic;
			break;
		case InformationCriterion::AICc:
			ic = result.metrics.aicc;
			break;
		case InformationCriterion::BIC:
			ic = result.metrics.bic;
			break;
		default:
			ic = result.metrics.aicc;
		}

		if (betterIC(ic, best_ic)) {
			best_ic = ic;
			best_result = std::move(result);
		}
	}

	if (!best_result.valid) {
		throw std::runtime_error("AutoARIMA failed to fit any valid model.");
	}

	// Step 4: Stepwise refinement (search neighbors iteratively)
	if (stepwise_) {
		bool improved = true;
		int iterations = 0;
		
		while (improved && iterations < max_iterations_) {
			improved = false;
			iterations++;

			auto new_best_config = findBestNeighbor(best_result.config, data, evaluator);
			
			// Check if we found a better neighbor
			if (new_best_config.p != best_result.config.p || 
			    new_best_config.q != best_result.config.q ||
			    new_best_config.P != best_result.config.P ||
			    new_best_config.Q != best_result.config.Q) {
				
				auto new_result = evaluator.evaluate(new_best_config);
				diagnostics_.models_evaluated++;
				
				if (new_result.valid) {
					double new_ic;
					switch (ic_) {
					case InformationCriterion::AIC:
						new_ic = new_result.metrics.aic;
						break;
					case InformationCriterion::AICc:
						new_ic = new_result.metrics.aicc;
						break;
					case InformationCriterion::BIC:
						new_ic = new_result.metrics.bic;
						break;
					default:
						new_ic = new_result.metrics.aicc;
					}

					if (betterIC(new_ic, best_ic)) {
						best_ic = new_ic;
						best_result = std::move(new_result);
						improved = true;
						ANOFOX_DEBUG("AutoARIMA: Found better model at iteration {}: ARIMA({},{},{}), IC={:.2f}",
						             iterations, best_result.config.p, best_result.config.d, 
						             best_result.config.q, best_ic);
					}
				} else {
					diagnostics_.models_failed++;
				}
			}
		}
	}

	// Step 5: Store final model
	components_.p = best_result.config.p;
	components_.d = best_result.config.d;
	components_.q = best_result.config.q;
	components_.P = best_result.config.P;
	components_.D = best_result.config.D;
	components_.Q = best_result.config.Q;
	components_.seasonal_period = seasonal_period_;
	components_.include_drift = best_result.config.include_drift;
	components_.include_constant = best_result.config.include_constant;

	metrics_ = best_result.metrics;
	fitted_model_ = std::move(best_result.model);

	// Extract parameters
	if (fitted_model_) {
		const auto &ar_coeffs = fitted_model_->arCoefficients();
		const auto &ma_coeffs = fitted_model_->maCoefficients();
		const auto &seasonal_ar_coeffs = fitted_model_->seasonalARCoefficients();
		const auto &seasonal_ma_coeffs = fitted_model_->seasonalMACoefficients();
		
		parameters_.ar_coefficients.assign(ar_coeffs.data(), ar_coeffs.data() + ar_coeffs.size());
		parameters_.ma_coefficients.assign(ma_coeffs.data(), ma_coeffs.data() + ma_coeffs.size());
		parameters_.seasonal_ar_coefficients.assign(seasonal_ar_coeffs.data(), seasonal_ar_coeffs.data() + seasonal_ar_coeffs.size());
		parameters_.seasonal_ma_coefficients.assign(seasonal_ma_coeffs.data(), seasonal_ma_coeffs.data() + seasonal_ma_coeffs.size());
		
		fitted_ = fitted_model_->residuals();  // Will be replaced with actual fitted values
		residuals_ = fitted_model_->residuals();
	}

	is_fitted_ = true;

	// Restore original max parameter values
	max_p_ = orig_max_p;
	max_q_ = orig_max_q;
	max_P_ = orig_max_P;
	max_Q_ = orig_max_Q;

	ANOFOX_INFO("AutoARIMA: Selected model ARIMA({},{},{})({},,{})[{}]",
	            components_.p, components_.d, components_.q,
	            components_.P, components_.D, components_.Q,
	            components_.seasonal_period);
	ANOFOX_INFO("AutoARIMA: Evaluated {} models ({} failed), final AICc={:.2f}",
	            diagnostics_.models_evaluated, diagnostics_.models_failed, metrics_.aicc);

	// Print selected parameters to stderr for debugging
	std::cerr << "[AutoARIMA] Selected ARIMA(" << components_.p << "," << components_.d << "," << components_.q
	          << ")(" << components_.P << "," << components_.D << "," << components_.Q << ")[" << components_.seasonal_period
	          << "], AICc=" << metrics_.aicc << std::endl;
}

core::Forecast AutoARIMA::predict(int horizon) {
	if (!is_fitted_ || !fitted_model_) {
		throw std::logic_error("AutoARIMA::predict called before fit.");
	}
	return fitted_model_->predict(horizon);
}

core::Forecast AutoARIMA::predictWithConfidence(int horizon, double confidence) {
	if (!is_fitted_ || !fitted_model_) {
		throw std::logic_error("AutoARIMA::predictWithConfidence called before fit.");
	}
	return fitted_model_->predictWithConfidence(horizon, confidence);
}

// Configuration methods
AutoARIMA &AutoARIMA::setMaxP(int max_p) {
	if (max_p < 0) {
		throw std::invalid_argument("max_p must be non-negative.");
	}
	max_p_ = max_p;
	return *this;
}

AutoARIMA &AutoARIMA::setMaxD(int max_d) {
	if (max_d < 0 || max_d > 2) {
		throw std::invalid_argument("max_d must be 0, 1, or 2.");
	}
	max_d_ = max_d;
	return *this;
}

AutoARIMA &AutoARIMA::setMaxQ(int max_q) {
	if (max_q < 0) {
		throw std::invalid_argument("max_q must be non-negative.");
	}
	max_q_ = max_q;
	return *this;
}

AutoARIMA &AutoARIMA::setMaxSeasonalP(int max_P) {
	if (max_P < 0) {
		throw std::invalid_argument("max_P must be non-negative.");
	}
	max_P_ = max_P;
	return *this;
}

AutoARIMA &AutoARIMA::setMaxSeasonalD(int max_D) {
	if (max_D < 0 || max_D > 1) {
		throw std::invalid_argument("max_D must be 0 or 1.");
	}
	max_D_ = max_D;
	return *this;
}

AutoARIMA &AutoARIMA::setMaxSeasonalQ(int max_Q) {
	if (max_Q < 0) {
		throw std::invalid_argument("max_Q must be non-negative.");
	}
	max_Q_ = max_Q;
	return *this;
}

AutoARIMA &AutoARIMA::setStartP(int start_p) {
	if (start_p < 0) {
		throw std::invalid_argument("start_p must be non-negative.");
	}
	start_p_ = start_p;
	return *this;
}

AutoARIMA &AutoARIMA::setStartQ(int start_q) {
	if (start_q < 0) {
		throw std::invalid_argument("start_q must be non-negative.");
	}
	start_q_ = start_q;
	return *this;
}

AutoARIMA &AutoARIMA::setStartSeasonalP(int start_P) {
	if (start_P < 0) {
		throw std::invalid_argument("start_P must be non-negative.");
	}
	start_P_ = start_P;
	return *this;
}

AutoARIMA &AutoARIMA::setStartSeasonalQ(int start_Q) {
	if (start_Q < 0) {
		throw std::invalid_argument("start_Q must be non-negative.");
	}
	start_Q_ = start_Q;
	return *this;
}

AutoARIMA &AutoARIMA::setStepwise(bool stepwise) {
	stepwise_ = stepwise;
	return *this;
}

AutoARIMA &AutoARIMA::setInformationCriterion(InformationCriterion ic) {
	ic_ = ic;
	return *this;
}

AutoARIMA &AutoARIMA::setAllowDrift(bool allow_drift) {
	allow_drift_ = allow_drift;
	return *this;
}

AutoARIMA &AutoARIMA::setAllowMeanTerm(bool allow_mean) {
	allow_mean_ = allow_mean;
	return *this;
}

AutoARIMA &AutoARIMA::setSeasonalTest(bool test_seasonal) {
	test_seasonal_ = test_seasonal;
	return *this;
}

AutoARIMA &AutoARIMA::setApproximation(bool use_approximation) {
	approximation_ = use_approximation;
	return *this;
}

AutoARIMA &AutoARIMA::setMaxIterations(int max_iter) {
	if (max_iter <= 0) {
		throw std::invalid_argument("max_iterations must be positive.");
	}
	max_iterations_ = max_iter;
	return *this;
}

// Accessor methods
const AutoARIMAComponents &AutoARIMA::components() const {
	if (!is_fitted_) {
		throw std::logic_error("AutoARIMA::components accessed before fit.");
	}
	return components_;
}

const AutoARIMAParameters &AutoARIMA::parameters() const {
	if (!is_fitted_) {
		throw std::logic_error("AutoARIMA::parameters accessed before fit.");
	}
	return parameters_;
}

const AutoARIMAMetrics &AutoARIMA::metrics() const {
	if (!is_fitted_) {
		throw std::logic_error("AutoARIMA::metrics accessed before fit.");
	}
	return metrics_;
}

const AutoARIMADiagnostics &AutoARIMA::diagnostics() const {
	if (!is_fitted_) {
		throw std::logic_error("AutoARIMA::diagnostics accessed before fit.");
	}
	return diagnostics_;
}

const std::vector<double> &AutoARIMA::fittedValues() const {
	if (!is_fitted_) {
		throw std::logic_error("AutoARIMA::fittedValues accessed before fit.");
	}
	return fitted_;
}

const std::vector<double> &AutoARIMA::residuals() const {
	if (!is_fitted_) {
		throw std::logic_error("AutoARIMA::residuals accessed before fit.");
	}
	return residuals_;
}

} // namespace anofoxtime::models

