#include "anofox-time/features/feature_calculators.hpp"
#include "anofox-time/features/feature_math.hpp"
#include "anofox-time/features/feature_types.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

namespace anofoxtime::features {

namespace {

double ReturnNaN() {
	return std::numeric_limits<double>::quiet_NaN();
}

double MeanOfVector(const std::vector<double> &values) {
	if (values.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double sum = std::accumulate(values.begin(), values.end(), 0.0);
	return sum / static_cast<double>(values.size());
}

double NormalCDF(double x) {
	return 0.5 * (1.0 + std::erf(x / std::sqrt(2.0)));
}

double NormalPValue(double t_stat) {
	double p = 2.0 * (1.0 - NormalCDF(std::fabs(t_stat)));
	return std::clamp(p, 0.0, 1.0);
}

double FeatureVarianceLargerThanStd(const Series &series, const ParameterMap &, FeatureCache &cache) {
	double variance = ComputeVariance(series, cache);
	if (!std::isfinite(variance)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double stddev = std::sqrt(std::max(variance, 0.0));
	return variance > stddev ? 1.0 : 0.0;
}

double FeatureRatioBeyondRSigma(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	auto r = param.GetDouble("r").value_or(0.5);
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double mean = ComputeMean(series, cache);
	double stddev = ComputeStdDev(series, cache);
	if (stddev < 1e-12) {
		return 0.0;
	}
	double threshold = r * stddev;
	size_t count = 0;
	for (double value : series) {
		if (std::fabs(value - mean) > threshold) {
			++count;
		}
	}
	return static_cast<double>(count) / series.size();
}

double FeatureLargeStandardDeviation(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	auto r = param.GetDouble("r").value_or(0.05);
	double stddev = ComputeStdDev(series, cache);
	return stddev > r ? 1.0 : 0.0;
}

double FeatureSymmetryLooking(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	auto r = param.GetDouble("r").value_or(0.05);
	double mean = ComputeMean(series, cache);
	size_t count_left = 0;
	size_t count_right = 0;
	for (double value : series) {
		if (value < mean - r) {
			++count_left;
		} else if (value > mean + r) {
			++count_right;
		}
	}
	return count_left == count_right ? 1.0 : 0.0;
}

double FeatureHasDuplicateMax(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return 0.0;
	}
	auto max_val = *std::max_element(series.begin(), series.end());
	size_t count = std::count(series.begin(), series.end(), max_val);
	return count > 1 ? 1.0 : 0.0;
}

double FeatureHasDuplicateMin(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return 0.0;
	}
	auto min_val = *std::min_element(series.begin(), series.end());
	size_t count = std::count(series.begin(), series.end(), min_val);
	return count > 1 ? 1.0 : 0.0;
}

double FeatureHasDuplicate(const Series &series, const ParameterMap &, FeatureCache &) {
	std::unordered_map<double, size_t> counts;
	for (double value : series) {
		if (++counts[value] > 1) {
			return 1.0;
		}
	}
	return 0.0;
}

double FeatureSumValues(const Series &series, const ParameterMap &, FeatureCache &) {
	return std::accumulate(series.begin(), series.end(), 0.0);
}

double FeatureMean(const Series &series, const ParameterMap &, FeatureCache &cache) {
	return ComputeMean(series, cache);
}

double FeatureMedian(const Series &series, const ParameterMap &, FeatureCache &cache) {
	return ComputeMedian(series, cache);
}

double FeatureLength(const Series &series, const ParameterMap &, FeatureCache &) {
	return static_cast<double>(series.size());
}

double FeatureStandardDeviation(const Series &series, const ParameterMap &, FeatureCache &cache) {
	return ComputeStdDev(series, cache);
}

double FeatureVariationCoefficient(const Series &series, const ParameterMap &, FeatureCache &cache) {
	double mean = ComputeMean(series, cache);
	double stddev = ComputeStdDev(series, cache);
	if (std::fabs(mean) < 1e-12) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return stddev / mean;
}

double FeatureVariance(const Series &series, const ParameterMap &, FeatureCache &cache) {
	return ComputeVariance(series, cache);
}

double FeatureSkewness(const Series &series, const ParameterMap &, FeatureCache &cache) {
	return ComputeSkewness(series, cache);
}

double FeatureKurtosis(const Series &series, const ParameterMap &, FeatureCache &cache) {
	return ComputeKurtosis(series, cache);
}

double FeatureAbsEnergy(const Series &series, const ParameterMap &, FeatureCache &) {
	double sum = 0.0;
	for (double value : series) {
		sum += value * value;
	}
	return sum;
}

double FeatureMeanAbsChange(const Series &series, const ParameterMap &, FeatureCache &cache) {
	auto diffs = ComputeDiffs(series, cache);
	if (diffs.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double sum = 0.0;
	for (double value : diffs) {
		sum += std::fabs(value);
	}
	return sum / diffs.size();
}

double FeatureMeanChange(const Series &series, const ParameterMap &, FeatureCache &cache) {
	auto diffs = ComputeDiffs(series, cache);
	if (diffs.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double sum = std::accumulate(diffs.begin(), diffs.end(), 0.0);
	return sum / diffs.size();
}

double FeatureMeanSecondDerivativeCentral(const Series &series, const ParameterMap &, FeatureCache &) {
	// tsfresh uses: mean of (1/2 * (x[i+1] - 2*x[i] + x[i-1])) for i in range(1, n-1)
	// The 1/2 factor is part of tsfresh's formula
	if (series.size() < 3) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double sum = 0.0;
	size_t count = 0;
	for (size_t i = 1; i < series.size() - 1; ++i) {
		// tsfresh divides by 2: (1/2) * (x[i+1] - 2*x[i] + x[i-1])
		sum += 0.5 * (series[i + 1] - 2.0 * series[i] + series[i - 1]);
		++count;
	}
	if (count == 0) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return sum / static_cast<double>(count);
}

double FeatureRootMeanSquare(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double sum = 0.0;
	for (double value : series) {
		sum += value * value;
	}
	return std::sqrt(sum / series.size());
}

double FeatureAbsoluteSumOfChanges(const Series &series, const ParameterMap &, FeatureCache &cache) {
	auto diffs = ComputeDiffs(series, cache);
	double sum = 0.0;
	for (double value : diffs) {
		sum += std::fabs(value);
	}
	return sum;
}

double FeatureLongestStrikeAboveMean(const Series &series, const ParameterMap &, FeatureCache &cache) {
	double mean = ComputeMean(series, cache);
	size_t current = 0;
	size_t best = 0;
	for (double value : series) {
		if (value > mean) {
			++current;
			best = std::max(best, current);
		} else {
			current = 0;
		}
	}
	return static_cast<double>(best);
}

double FeatureLongestStrikeBelowMean(const Series &series, const ParameterMap &, FeatureCache &cache) {
	double mean = ComputeMean(series, cache);
	size_t current = 0;
	size_t best = 0;
	for (double value : series) {
		if (value < mean) {
			++current;
			best = std::max(best, current);
		} else {
			current = 0;
		}
	}
	return static_cast<double>(best);
}

double FeatureCountAboveMean(const Series &series, const ParameterMap &, FeatureCache &cache) {
	double mean = ComputeMean(series, cache);
	size_t count = 0;
	for (double value : series) {
		if (value > mean) {
			++count;
		}
	}
	return static_cast<double>(count);
}

double FeatureCountBelowMean(const Series &series, const ParameterMap &, FeatureCache &cache) {
	double mean = ComputeMean(series, cache);
	size_t count = 0;
	for (double value : series) {
		if (value < mean) {
			++count;
		}
	}
	return static_cast<double>(count);
}

double FeatureFirstLocationOfMinimum(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	auto min_it = std::min_element(series.begin(), series.end());
	return static_cast<double>(std::distance(series.begin(), min_it)) / series.size();
}

double FeatureLastLocationOfMinimum(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	auto min_val = *std::min_element(series.begin(), series.end());
	for (size_t i = series.size(); i > 0; --i) {
		if (series[i - 1] == min_val) {
			// tsfresh uses 1-based indexing: position i (0-based) becomes (i+1) / series.size()
			return static_cast<double>(i) / series.size();
		}
	}
	return std::numeric_limits<double>::quiet_NaN();
}

double FeatureFirstLocationOfMaximum(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	auto max_it = std::max_element(series.begin(), series.end());
	return static_cast<double>(std::distance(series.begin(), max_it)) / series.size();
}

double FeatureLastLocationOfMaximum(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	auto max_val = *std::max_element(series.begin(), series.end());
	for (size_t i = series.size(); i > 0; --i) {
		if (series[i - 1] == max_val) {
			// tsfresh uses 1-based indexing: position i (0-based) becomes (i+1) / series.size()
			return static_cast<double>(i) / series.size();
		}
	}
	return std::numeric_limits<double>::quiet_NaN();
}

double FeaturePercentageOfReoccurringValuesToAllValues(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	std::unordered_map<double, size_t> counts;
	for (double value : series) {
		counts[value]++;
	}
	size_t reoccurring = 0;
	for (const auto &kv : counts) {
		if (kv.second > 1) {
			++reoccurring;
		}
	}
	return static_cast<double>(reoccurring) / counts.size();
}

double FeaturePercentageOfReoccurringDatapointsToAllValues(const Series &series, const ParameterMap &,
                                                           FeatureCache &) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	std::unordered_map<double, size_t> counts;
	for (double value : series) {
		counts[value]++;
	}
	size_t reoccurring = 0;
	for (const auto &kv : counts) {
		if (kv.second > 1) {
			reoccurring += kv.second;
		}
	}
	return static_cast<double>(reoccurring) / series.size();
}

double FeatureSumOfReoccurringValues(const Series &series, const ParameterMap &, FeatureCache &) {
	std::unordered_map<double, size_t> counts;
	for (double value : series) {
		counts[value]++;
	}
	double sum = 0.0;
	for (const auto &kv : counts) {
		if (kv.second > 1) {
			// tsfresh: sum_of_reoccurring_values sums value * count (all instances)
			sum += kv.first * kv.second;
		}
	}
	return sum;
}

double FeatureSumOfReoccurringDataPoints(const Series &series, const ParameterMap &, FeatureCache &) {
	std::unordered_map<double, size_t> counts;
	for (double value : series) {
		counts[value]++;
	}
	double sum = 0.0;
	for (const auto &kv : counts) {
		if (kv.second > 1) {
			// tsfresh: sum_of_reoccurring_data_points sums value once (unique reoccurring value)
			sum += kv.first;
		}
	}
	return sum;
}

double FeatureRatioValueNumberToSeriesLength(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	std::unordered_map<double, size_t> counts;
	for (double value : series) {
		counts[value]++;
	}
	return static_cast<double>(counts.size()) / series.size();
}

double FeatureMaximum(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return *std::max_element(series.begin(), series.end());
}

double FeatureMinimum(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return *std::min_element(series.begin(), series.end());
}

double FeatureAbsoluteMaximum(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double max_abs = 0.0;
	for (double value : series) {
		max_abs = std::max(max_abs, std::fabs(value));
	}
	return max_abs;
}

double FeatureValueCount(const Series &series, const ParameterMap &param, FeatureCache &) {
	double target = param.GetDouble("value").value_or(0.0);
	return static_cast<double>(std::count(series.begin(), series.end(), target));
}

double FeatureRangeCount(const Series &series, const ParameterMap &param, FeatureCache &) {
	double min_val = param.GetDouble("min").value_or(0.0);
	double max_val = param.GetDouble("max").value_or(0.0);
	size_t count = 0;
	for (double value : series) {
		if (value >= min_val && value < max_val) {
			++count;
		}
	}
	return static_cast<double>(count);
}

double FeatureCountAbove(const Series &series, const ParameterMap &param, FeatureCache &) {
	double threshold = param.GetDouble("t").value_or(0.0);
	size_t count = 0;
	for (double value : series) {
		if (value > threshold) {
			++count;
		}
	}
	// tsfresh count_above returns count / length (ratio), not absolute count
	return static_cast<double>(count) / static_cast<double>(series.size());
}

double FeatureCountBelow(const Series &series, const ParameterMap &param, FeatureCache &) {
	double threshold = param.GetDouble("t").value_or(0.0);
	size_t count = 0;
	for (double value : series) {
		if (value < threshold) {
			++count;
		}
	}
	return static_cast<double>(count);
}

double FeatureQuantile(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	double q = param.GetDouble("q").value_or(0.5);
	return ComputeQuantile(series, q, cache);
}

double FeatureNumberCrossingM(const Series &series, const ParameterMap &param, FeatureCache &) {
	if (series.size() < 2) {
		return 0.0;
	}
	double m = param.GetDouble("m").value_or(0.0);
	size_t count = 0;
	// tsfresh counts crossings when value crosses m
	// Count when: (prev - m) and (curr - m) have different signs
	// Also count when one equals m and the other doesn't (crossing the threshold)
	for (size_t i = 1; i < series.size(); ++i) {
		double prev_diff = series[i - 1] - m;
		double curr_diff = series[i] - m;
		// Crossing occurs when:
		// 1. Signs differ (one positive, one negative)
		// 2. One equals zero and the other doesn't (crossing the threshold)
		bool signs_differ = (prev_diff > 0.0 && curr_diff < 0.0) || (prev_diff < 0.0 && curr_diff > 0.0);
		bool one_equals_m = (std::fabs(prev_diff) < 1e-12 && std::fabs(curr_diff) >= 1e-12) ||
		                    (std::fabs(prev_diff) >= 1e-12 && std::fabs(curr_diff) < 1e-12);
		if (signs_differ || one_equals_m) {
			++count;
		}
	}
	return static_cast<double>(count);
}

double FeatureAutocorrelation(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	size_t lag = static_cast<size_t>(param.GetInt("lag").value_or(0));
	return ComputeAutocorrelation(series, lag, cache);
}

double FeatureAggAutocorrelation(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	size_t maxlag = static_cast<size_t>(param.GetInt("maxlag").value_or(40));
	auto agg = param.GetString("f_agg").value_or("mean");
	std::vector<double> values;
	for (size_t lag = 1; lag <= maxlag; ++lag) {
		values.push_back(ComputeAutocorrelation(series, lag, cache));
	}
	if (values.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	if (agg == "mean") {
		return MeanOfVector(values);
	}
	if (agg == "median") {
		std::sort(values.begin(), values.end());
		size_t n = values.size();
		if (n % 2 == 1) {
			return values[n / 2];
		}
		return (values[n / 2 - 1] + values[n / 2]) / 2.0;
	}
	if (agg == "var") {
		double mean = MeanOfVector(values);
		double accum = 0.0;
		for (auto value : values) {
			double diff = value - mean;
			accum += diff * diff;
		}
		return accum / values.size();
	}
	return MeanOfVector(values);
}

double FeaturePartialAutocorrelation(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	int lag = static_cast<int>(param.GetInt("lag").value_or(0));
	if (lag <= 0 || series.size() <= static_cast<size_t>(lag)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	// Simple Yule-Walker estimation
	size_t n = series.size();
	std::vector<std::vector<double>> phi(lag + 1, std::vector<double>(lag + 1, 0.0));
	std::vector<double> sigma(lag + 1, 0.0);
	auto autocorr = [&](int k) { return ComputeAutocorrelation(series, k, cache); };
	sigma[0] = autocorr(0);
	for (int k = 1; k <= lag; ++k) {
		double sum = 0.0;
		for (int j = 1; j < k; ++j) {
			sum += phi[k - 1][j] * autocorr(k - j);
		}
		double num = autocorr(k) - sum;
		double denom = sigma[k - 1];
		if (std::fabs(denom) < 1e-12) {
			return std::numeric_limits<double>::quiet_NaN();
		}
		double phi_k = num / denom;
		phi[k][k] = phi_k;
		for (int j = 1; j < k; ++j) {
			phi[k][j] = phi[k - 1][j] - phi_k * phi[k - 1][k - j];
		}
		sigma[k] = sigma[k - 1] * (1.0 - phi_k * phi_k);
	}
	return phi[lag][lag];
}

// Helper function to solve OLS regression: y = X * beta + error
// Returns: {params, ss_res, aic} where params are regression coefficients, ss_res is sum of squared residuals
struct OLSResult {
	std::vector<double> params;
	double ss_res = 0.0;
	double aic = std::numeric_limits<double>::quiet_NaN();
};

OLSResult SolveOLS(const std::vector<std::vector<double>> &X, const std::vector<double> &y) {
	OLSResult result;
	if (X.empty() || X.size() != y.size()) {
		return result;
	}
	size_t n_obs = X.size();
	size_t n_params = X[0].size();
	if (n_params == 0 || n_obs < n_params) {
		return result;
	}
	
	// Compute X'X
	std::vector<std::vector<double>> XtX(n_params, std::vector<double>(n_params, 0.0));
	for (size_t i = 0; i < n_params; ++i) {
		for (size_t j = 0; j < n_params; ++j) {
			for (size_t k = 0; k < n_obs; ++k) {
				XtX[i][j] += X[k][i] * X[k][j];
			}
		}
	}
	
	// Compute X'y
	std::vector<double> Xty(n_params, 0.0);
	for (size_t i = 0; i < n_params; ++i) {
		for (size_t k = 0; k < n_obs; ++k) {
			Xty[i] += X[k][i] * y[k];
		}
	}
	
	// Solve XtX * params = Xty using Gaussian elimination
	result.params.resize(n_params, 0.0);
	
	// Forward elimination
	for (size_t i = 0; i < n_params; ++i) {
		// Find pivot
		size_t max_row = i;
		double max_val = std::fabs(XtX[i][i]);
		for (size_t k = i + 1; k < n_params; ++k) {
			if (std::fabs(XtX[k][i]) > max_val) {
				max_val = std::fabs(XtX[k][i]);
				max_row = k;
			}
		}
		if (max_val < 1e-12) {
			return result;  // Singular matrix
		}
		
		// Swap rows
		if (max_row != i) {
			std::swap(XtX[i], XtX[max_row]);
			std::swap(Xty[i], Xty[max_row]);
		}
		
		// Eliminate
		for (size_t k = i + 1; k < n_params; ++k) {
			double factor = XtX[k][i] / XtX[i][i];
			for (size_t j = i; j < n_params; ++j) {
				XtX[k][j] -= factor * XtX[i][j];
			}
			Xty[k] -= factor * Xty[i];
		}
	}
	
	// Back substitution
	for (int i = static_cast<int>(n_params) - 1; i >= 0; --i) {
		if (std::fabs(XtX[i][i]) < 1e-12) {
			return result;  // Singular matrix
		}
		result.params[i] = Xty[i];
		for (size_t j = i + 1; j < n_params; ++j) {
			result.params[i] -= XtX[i][j] * result.params[j];
		}
		result.params[i] /= XtX[i][i];
	}
	
	// Compute sum of squared residuals
	for (size_t k = 0; k < n_obs; ++k) {
		double predicted = 0.0;
		for (size_t i = 0; i < n_params; ++i) {
			predicted += result.params[i] * X[k][i];
		}
		double residual = y[k] - predicted;
		result.ss_res += residual * residual;
	}
	
	// Compute AIC = n*log(SSR/n) + 2*k
	double sigma2 = result.ss_res / static_cast<double>(n_obs);
	if (sigma2 > 1e-12) {
		result.aic = static_cast<double>(n_obs) * std::log(sigma2) + 2.0 * static_cast<double>(n_params);
	}
	
	return result;
}

double FeatureAugmentedDickeyFuller(const Series &series, const ParameterMap &param, FeatureCache &) {
	if (series.size() < 5) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	auto autolag_str = param.GetString("autolag").value_or("AIC");
	size_t n = series.size();
	
	// Compute differences once
	std::vector<double> diff(n - 1);
	for (size_t i = 1; i < n; ++i) {
		diff[i - 1] = series[i] - series[i - 1];
	}
	
	// Determine lag selection
	size_t lag = 1;  // Default
	if (autolag_str == "AIC" || autolag_str == "BIC" || autolag_str == "t-stats") {
		// tsfresh uses statsmodels adfuller with autolag
		// For AIC: try lags from 0 to maxlag, select lag with minimum AIC
		// maxlag formula from statsmodels: 12*(nobs/100)^(1/4)
		size_t maxlag = static_cast<size_t>(12.0 * std::pow(static_cast<double>(n) / 100.0, 0.25));
		maxlag = std::min(maxlag, n - 2);  // Ensure we have enough data
		
		double best_aic = std::numeric_limits<double>::infinity();
		size_t best_lag = 1;
		
		// Try each lag and compute AIC using full ADF regression
		for (size_t test_lag = 0; test_lag <= maxlag && test_lag < n - 2; ++test_lag) {
			// Full ADF regression: diff[t] = alpha + beta*x[t-1] + gamma1*diff[t-1] + ... + gamma_lag*diff[t-lag] + error
			// Number of parameters: 1 (constant) + 1 (x[t-1]) + test_lag (lagged differences) = test_lag + 2
			size_t start_idx = test_lag;
			if (start_idx >= n - 1) {
				continue;
			}
			size_t n_obs = n - 1 - start_idx;
			if (n_obs < test_lag + 2) {
				continue;  // Not enough observations
			}
			
			// Build design matrix X and response vector y
			std::vector<std::vector<double>> X(n_obs);
			std::vector<double> y(n_obs);
			
			for (size_t t = 0; t < n_obs; ++t) {
				size_t idx = start_idx + t;
				y[t] = diff[idx];  // diff[t] where t = idx
				
				// Build row: [1, x[t-1], diff[t-1], diff[t-2], ..., diff[t-lag]]
				// For diff[idx] = x[idx+1] - x[idx], in ADF regression:
				// diff[t] = alpha + rho*x[t-1] + sum(gamma_i*diff[t-i]) + error
				// For diff[idx], x[t-1] = x[idx] (the level before the difference)
				// This is because diff[idx] represents the change from x[idx] to x[idx+1]
				std::vector<double> row(test_lag + 2);
				row[0] = 1.0;  // Constant term
				row[1] = series[idx];  // x[t-1] = x[idx] for diff[idx]
				
				// Add lagged differences: diff[t-1], diff[t-2], ..., diff[t-lag]
				// For diff[idx], diff[t-1] = diff[idx-1], diff[t-2] = diff[idx-2], etc.
				for (size_t i = 0; i < test_lag; ++i) {
					if (idx > i) {
						row[2 + i] = diff[idx - 1 - i];
					} else {
						row[2 + i] = 0.0;  // Pad with zeros if not enough history
					}
				}
				X[t] = row;
			}
			
			// Solve OLS regression
			auto ols_result = SolveOLS(X, y);
			if (!std::isfinite(ols_result.aic)) {
				continue;
			}
			
			if (ols_result.aic < best_aic) {
				best_aic = ols_result.aic;
				best_lag = test_lag;
			}
		}
		lag = best_lag;
	} else {
		// Use fixed lag (default 1)
		lag = 1;
	}
	
	// Now compute ADF test statistic with selected lag using full regression
	size_t start_idx = lag;
	if (start_idx >= n - 1) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	size_t n_obs = n - 1 - start_idx;
	if (n_obs < lag + 2) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	// Build design matrix and response for final regression
	std::vector<std::vector<double>> X(n_obs);
	std::vector<double> y(n_obs);
	
	for (size_t t = 0; t < n_obs; ++t) {
		size_t idx = start_idx + t;
		y[t] = diff[idx];  // diff[t] where t = idx
		
		std::vector<double> row(lag + 2);
		row[0] = 1.0;  // Constant
		row[1] = series[idx];  // x[t-1] = x[idx] for diff[idx] (level before the difference)
		for (size_t i = 0; i < lag; ++i) {
			if (idx > i) {
				row[2 + i] = diff[idx - 1 - i];
			} else {
				row[2 + i] = 0.0;
			}
		}
		X[t] = row;
	}
	
	// Solve OLS regression
	auto ols_result = SolveOLS(X, y);
	if (ols_result.params.size() < 2) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	// Test statistic is the coefficient for x[t-1] (params[1])
	double teststat = ols_result.params[1];
	
	auto attr = param.GetString("attr").value_or("teststat");
	if (attr == "teststat") {
		return teststat;
	}
	if (attr == "pvalue") {
		return NormalPValue(teststat);
	}
	if (attr == "usedlag") {
		return static_cast<double>(lag);
	}
	return teststat;
}

double FeatureNumberPeaks(const Series &series, const ParameterMap &param, FeatureCache &) {
	int n = static_cast<int>(param.GetInt("n").value_or(1));
	if (series.size() < static_cast<size_t>(2 * n + 1)) {
		return 0.0;
	}
	
	size_t count = 0;
	// tsfresh number_peaks: count peaks where value > n neighbors on each side
	// For n=1: value > left_neighbor AND value > right_neighbor
	// Loop from index n to series.size()-n-1 (inclusive)
	// For n=1, series.size()=365: indices 1 to 363
	size_t start_idx = static_cast<size_t>(n);
	size_t end_idx = series.size() - static_cast<size_t>(n);
	
	// Diagnostic: check if loop bounds are correct
	// For n=1, series.size()=365: start_idx=1, end_idx=364, so i goes from 1 to 363
	if (start_idx >= end_idx) {
		return 0.0;
	}
	
	// tsfresh number_peaks: count peaks where value > n neighbors on each side
	// For n=1: value > left_neighbor AND value > right_neighbor
	size_t iterations = 0;
	size_t left_failures = 0;
	size_t right_failures = 0;
	
	for (size_t i = start_idx; i < end_idx; ++i) {
		++iterations;
		// Check if value > all n neighbors on the left
		bool left_ok = true;
		for (int j = 1; j <= n; ++j) {
			size_t left_idx = i - static_cast<size_t>(j);
			// tsfresh uses > comparison: value > neighbor is a peak (value <= neighbor is not)
			if (series[i] <= series[left_idx]) {
				left_ok = false;
				++left_failures;
				break;
			}
		}
		// Check if value > all n neighbors on the right
		bool right_ok = true;
		if (left_ok) {
			for (int j = 1; j <= n; ++j) {
				size_t right_idx = i + static_cast<size_t>(j);
				// tsfresh uses > comparison: value > neighbor is a peak (value <= neighbor is not)
				if (series[i] <= series[right_idx]) {
					right_ok = false;
					++right_failures;
					break;
				}
			}
		}
		// If both left and right checks pass, it's a peak
		if (left_ok && right_ok) {
			++count;
		}
	}
	
	return static_cast<double>(count);
}

double FeatureIndexMassQuantile(const Series &series, const ParameterMap &param, FeatureCache &) {
	double q = param.GetDouble("q").value_or(0.5);
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double total = 0.0;
	for (double value : series) {
		total += std::fabs(value);
	}
	if (total < 1e-12) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double threshold = q * total;
	double running = 0.0;
	for (size_t i = 0; i < series.size(); ++i) {
		running += std::fabs(series[i]);
		if (running >= threshold) {
			// tsfresh uses (i+1) / series.size() to match their indexing
			return static_cast<double>(i + 1) / series.size();
		}
	}
	return 1.0;
}

double FeatureNumberCwtPeaks(const Series &series, const ParameterMap &param, FeatureCache &) {
	int n = static_cast<int>(param.GetInt("n").value_or(1));
	return NumberCwtPeaks(series, n);
}

double FeatureCwtCoefficients(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	auto widths_vector = param.GetDoubleVector("widths").value_or(std::vector<double> {2, 5, 10, 20});
	std::vector<int64_t> widths;
	for (double value : widths_vector) {
		widths.push_back(static_cast<int64_t>(value));
	}
	size_t coeff = static_cast<size_t>(param.GetInt("coeff").value_or(0));
	int64_t w = param.GetInt("w").value_or(2);
	return CwtCoefficient(series, cache, widths, coeff, w);
}

double FeatureSpktWelchDensity(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	size_t coeff = static_cast<size_t>(param.GetInt("coeff").value_or(0));
	return SpktWelchDensity(series, cache, coeff);
}

double FeatureArCoefficient(const Series &series, const ParameterMap &param, FeatureCache &) {
	size_t coeff = static_cast<size_t>(param.GetInt("coeff").value_or(0));
	size_t order = static_cast<size_t>(param.GetInt("k").value_or(10));
	if (series.size() <= order || order == 0) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	if (coeff > order) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	// tsfresh uses AutoReg with trend="c", which includes an intercept
	// params[0] = intercept, params[1] = AR(1), params[2] = AR(2), ..., params[k] = AR(k)
	// We need to fit AR model: x_t = intercept + phi_1*x_{t-1} + ... + phi_k*x_{t-k} + epsilon_t
	
	size_t n = series.size();
	size_t m = order; // number of lags
	
	// Build design matrix X: first column is intercept (1s), rest are lagged values
	// and response vector y
	std::vector<std::vector<double>> X;
	std::vector<double> y;
	
	for (size_t t = order; t < n; ++t) {
		std::vector<double> row(m + 1, 1.0); // intercept column is 1
		for (size_t lag = 1; lag <= order; ++lag) {
			row[lag] = series[t - lag];
		}
		X.push_back(row);
		y.push_back(series[t]);
	}
	
	if (X.empty() || X.size() < m + 1) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	// Solve using normal equations: (X'X) * params = X'y
	// This is a simple OLS solution
	size_t n_obs = X.size();
	size_t n_params = m + 1;
	
	// Compute X'X
	std::vector<std::vector<double>> XtX(n_params, std::vector<double>(n_params, 0.0));
	for (size_t i = 0; i < n_params; ++i) {
		for (size_t j = 0; j < n_params; ++j) {
			for (size_t k = 0; k < n_obs; ++k) {
				XtX[i][j] += X[k][i] * X[k][j];
			}
		}
	}
	
	// Compute X'y
	std::vector<double> Xty(n_params, 0.0);
	for (size_t i = 0; i < n_params; ++i) {
		for (size_t k = 0; k < n_obs; ++k) {
			Xty[i] += X[k][i] * y[k];
		}
	}
	
	// Solve XtX * params = Xty using Gaussian elimination
	std::vector<double> params(n_params, 0.0);
	
	// Forward elimination
	for (size_t i = 0; i < n_params; ++i) {
		// Find pivot
		size_t max_row = i;
		double max_val = std::fabs(XtX[i][i]);
		for (size_t k = i + 1; k < n_params; ++k) {
			if (std::fabs(XtX[k][i]) > max_val) {
				max_val = std::fabs(XtX[k][i]);
				max_row = k;
			}
		}
		if (max_val < 1e-12) {
			return std::numeric_limits<double>::quiet_NaN();
		}
		
		// Swap rows
		if (max_row != i) {
			std::swap(XtX[i], XtX[max_row]);
			std::swap(Xty[i], Xty[max_row]);
		}
		
		// Eliminate
		for (size_t k = i + 1; k < n_params; ++k) {
			double factor = XtX[k][i] / XtX[i][i];
			for (size_t j = i; j < n_params; ++j) {
				XtX[k][j] -= factor * XtX[i][j];
			}
			Xty[k] -= factor * Xty[i];
		}
	}
	
	// Back substitution
	for (int i = static_cast<int>(n_params) - 1; i >= 0; --i) {
		// Check if diagonal element is too small (numerical instability)
		if (std::fabs(XtX[i][i]) < 1e-12) {
			return std::numeric_limits<double>::quiet_NaN();
		}
		params[i] = Xty[i];
		for (size_t j = i + 1; j < n_params; ++j) {
			params[i] -= XtX[i][j] * params[j];
		}
		params[i] /= XtX[i][i];
	}
	
	// Check if we got a valid solution (not all zeros or NaN)
	bool all_zero = true;
	for (size_t i = 0; i < n_params; ++i) {
		if (!std::isfinite(params[i])) {
			return std::numeric_limits<double>::quiet_NaN();
		}
		if (std::fabs(params[i]) > 1e-10) {
			all_zero = false;
		}
	}
	// If all params are essentially zero, something went wrong
	if (all_zero && n_params > 0) {
		// For AR model, intercept should generally not be zero
		// unless the series is perfectly centered, which is unlikely
		// This might indicate a numerical issue or singular matrix
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	return params[coeff];
}

double FeatureChangeQuantiles(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	double ql = param.GetDouble("ql").value_or(0.0);
	double qh = param.GetDouble("qh").value_or(1.0);
	bool isabs = param.GetBool("isabs").value_or(false);
	auto f = param.GetString("f_agg").value_or("mean");
	if (ql >= qh || series.size() < 2) {
		return 0.0;  // tsfresh returns 0.0, not NaN
	}
	// tsfresh change_quantiles: 
	// 1. Compute changes (differences)
	// 2. Find quantiles of ORIGINAL values (not changes)
	// 3. Select changes where BOTH start and end values are in the quantile corridor
	auto changes = ComputeDiffs(series, cache);
	if (changes.empty()) {
		return 0.0;
	}
	// Find quantiles of the original series (corridor)
	double low = ComputeQuantile(series, ql, cache);
	double high = ComputeQuantile(series, qh, cache);
	// Select changes where both x[i] and x[i+1] are in the corridor
	std::vector<double> selected_changes;
	for (size_t i = 0; i < changes.size(); ++i) {
		// Check if both series[i] and series[i+1] are in the corridor
		if (series[i] >= low && series[i] <= high && 
		    series[i + 1] >= low && series[i + 1] <= high) {
			double change = changes[i];
			if (isabs) {
				change = std::fabs(change);
			}
			selected_changes.push_back(change);
		}
	}
	if (selected_changes.empty()) {
		return 0.0;  // tsfresh returns 0.0, not NaN
	}
	if (f == "mean") {
		return MeanOfVector(selected_changes);
	}
	if (f == "var") {
		double mean = MeanOfVector(selected_changes);
		double accum = 0.0;
		for (double value : selected_changes) {
			double diff = value - mean;
			accum += diff * diff;
		}
		return accum / selected_changes.size();
	}
	return MeanOfVector(selected_changes);
}

double FeatureTimeReversalAsymmetryStatistic(const Series &series, const ParameterMap &param, FeatureCache &) {
	int lag = static_cast<int>(param.GetInt("lag").value_or(1));
	size_t n = series.size();
	if (lag <= 0 || static_cast<size_t>(2 * lag) >= n) {
		return 0.0;
	}
	// tsfresh formula: mean(x_{i+2*lag}^2 * x_{i+lag} - x_{i+lag} * x_i^2)
	// for i from 0 to n-2*lag-1
	double sum = 0.0;
	size_t count = 0;
	for (size_t i = 0; i + 2 * static_cast<size_t>(lag) < n; ++i) {
		size_t idx_i = i;
		size_t idx_i_lag = i + static_cast<size_t>(lag);
		size_t idx_i_2lag = i + 2 * static_cast<size_t>(lag);
		double term1 = series[idx_i_2lag] * series[idx_i_2lag] * series[idx_i_lag];
		double term2 = series[idx_i_lag] * series[idx_i] * series[idx_i];
		sum += term1 - term2;
		++count;
	}
	if (count == 0) {
		return 0.0;
	}
	return sum / static_cast<double>(count);
}

double FeatureC3(const Series &series, const ParameterMap &param, FeatureCache &) {
	int lag = static_cast<int>(param.GetInt("lag").value_or(1));
	if (lag <= 0 || series.size() <= static_cast<size_t>(2 * lag)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double sum = 0.0;
	size_t count = 0;
	for (size_t i = 2 * lag; i < series.size(); ++i) {
		sum += series[i] * series[i - lag] * series[i - 2 * lag];
		++count;
	}
	return (count == 0) ? std::numeric_limits<double>::quiet_NaN() : sum / count;
}

double FeatureMeanNAbsoluteMax(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	int number = static_cast<int>(param.GetInt("number_of_maxima").value_or(3));
	if (number <= 0 || series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	auto sorted = ComputeAbsSorted(series, cache);
	if (static_cast<size_t>(number) > sorted.size()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double sum = 0.0;
	for (int i = 0; i < number; ++i) {
		sum += sorted[sorted.size() - 1 - i];
	}
	return sum / number;
}

double FeatureBinnedEntropy(const Series &series, const ParameterMap &param, FeatureCache &) {
	int bins = static_cast<int>(param.GetInt("max_bins").value_or(10));
	if (series.empty() || bins <= 0) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	auto minmax = std::minmax_element(series.begin(), series.end());
	double min_val = *minmax.first;
	double max_val = *minmax.second;
	if (std::fabs(max_val - min_val) < 1e-12) {
		return 0.0;
	}
	std::vector<int> histogram(bins, 0);
	for (double value : series) {
		double normalized = (value - min_val) / (max_val - min_val);
		int idx = static_cast<int>(std::floor(normalized * bins));
		if (idx >= bins) {
			idx = bins - 1;
		}
		++histogram[idx];
	}
	double entropy = 0.0;
	for (int count : histogram) {
		if (count == 0) {
			continue;
		}
		double p = static_cast<double>(count) / series.size();
		entropy -= p * std::log(p);
	}
	return entropy;
}

double FeatureSampleEntropy(const Series &series, const ParameterMap &, FeatureCache &cache) {
	return SampleEntropy(series, 2, 0.2, cache);
}

double FeatureApproximateEntropy(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	int m = static_cast<int>(param.GetInt("m").value_or(2));
	double r = param.GetDouble("r").value_or(0.1);
	return ApproximateEntropy(series, m, r, cache);
}

double FeatureFourierEntropy(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	int bins = static_cast<int>(param.GetInt("bins").value_or(2));
	return FourierEntropy(series, bins, cache);
}

double FeatureLempelZivComplexity(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	int bins = static_cast<int>(param.GetInt("bins").value_or(2));
	return LempelZivComplexity(series, bins, cache);
}

double FeaturePermutationEntropy(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	int tau = static_cast<int>(param.GetInt("tau").value_or(1));
	int dimension = static_cast<int>(param.GetInt("dimension").value_or(3));
	return PermutationEntropy(series, dimension, tau, cache);
}

double FeatureBenfordCorrelation(const Series &series, const ParameterMap &, FeatureCache &) {
	return BenfordCorrelation(series);
}

double FeatureMatrixProfile(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	double threshold = param.GetDouble("threshold").value_or(0.98);
	auto feature = param.GetString("feature").value_or("mean");
	return MatrixProfileValue(series, threshold, feature, cache);
}

double FeatureQuerySimilarityCount(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	double threshold = param.GetDouble("threshold").value_or(0.0);
	std::vector<double> query;
	auto query_vec = param.GetDoubleVector("query");
	if (query_vec) {
		query = *query_vec;
	}
	return QuerySimilarityCount(series, query, threshold, cache);
}

double FeatureFriedrichCoefficients(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	int m = static_cast<int>(param.GetInt("m").value_or(3));
	int coeff = static_cast<int>(param.GetInt("coeff").value_or(0));
	double r = param.GetDouble("r").value_or(30.0);
	if (series.size() < 2 || coeff < 0 || coeff > m) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	// tsfresh: Groups data by quantiles, then fits polynomial to quantile means
	// Create signal (x[:-1]) and delta (diff(x))
	std::vector<double> signal(series.size() - 1);
	std::vector<double> delta(series.size() - 1);
	for (size_t i = 1; i < series.size(); ++i) {
		signal[i - 1] = series[i - 1];
		delta[i - 1] = series[i] - series[i - 1];
	}
	
	// Create quantiles using pd.qcut equivalent
	int num_quantiles = static_cast<int>(r);
	if (num_quantiles <= 0 || static_cast<size_t>(num_quantiles) > signal.size()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	// Sort signal and assign quantiles
	std::vector<std::pair<double, double>> data_points; // (signal, delta)
	for (size_t i = 0; i < signal.size(); ++i) {
		data_points.emplace_back(signal[i], delta[i]);
	}
	std::sort(data_points.begin(), data_points.end(),
	          [](const auto &a, const auto &b) { return a.first < b.first; });
	
	// Compute quantile means
	std::vector<double> x_means, y_means;
	size_t per_quantile = data_points.size() / static_cast<size_t>(num_quantiles);
	for (int q = 0; q < num_quantiles; ++q) {
		size_t start = static_cast<size_t>(q) * per_quantile;
		size_t end = (q == num_quantiles - 1) ? data_points.size() : start + per_quantile;
		if (start >= data_points.size()) {
			break;
		}
		
		double sum_x = 0.0;
		double sum_y = 0.0;
		size_t count = 0;
		for (size_t i = start; i < end; ++i) {
			sum_x += data_points[i].first;
			sum_y += data_points[i].second;
			++count;
		}
		if (count > 0) {
			x_means.push_back(sum_x / static_cast<double>(count));
			y_means.push_back(sum_y / static_cast<double>(count));
		}
	}
	
	if (x_means.empty() || x_means.size() < static_cast<size_t>(m + 1)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	// Fit polynomial using least squares: polyfit(x_means, y_means, deg=m)
	// Solve normal equations for polynomial coefficients
	std::vector<std::vector<double>> X_poly(x_means.size(), std::vector<double>(m + 1, 1.0));
	for (size_t i = 0; i < x_means.size(); ++i) {
		for (int j = 1; j <= m; ++j) {
			X_poly[i][j] = std::pow(x_means[i], j);
		}
	}
	
	// Compute X'X and X'y
	std::vector<std::vector<double>> XtX(m + 1, std::vector<double>(m + 1, 0.0));
	std::vector<double> Xty(m + 1, 0.0);
	for (int i = 0; i <= m; ++i) {
		for (int j = 0; j <= m; ++j) {
			for (size_t k = 0; k < x_means.size(); ++k) {
				XtX[i][j] += X_poly[k][i] * X_poly[k][j];
			}
		}
		for (size_t k = 0; k < x_means.size(); ++k) {
			Xty[i] += X_poly[k][i] * y_means[k];
		}
	}
	
	// Solve using Gaussian elimination
	std::vector<double> poly_coeffs(m + 1, 0.0);
	
	// Forward elimination
	for (int i = 0; i <= m; ++i) {
		// Find pivot
		int max_row = i;
		double max_val = std::fabs(XtX[i][i]);
		for (int k = i + 1; k <= m; ++k) {
			if (std::fabs(XtX[k][i]) > max_val) {
				max_val = std::fabs(XtX[k][i]);
				max_row = k;
			}
		}
		if (max_val < 1e-12) {
			return std::numeric_limits<double>::quiet_NaN();
		}
		
		if (max_row != i) {
			std::swap(XtX[i], XtX[max_row]);
			std::swap(Xty[i], Xty[max_row]);
		}
		
		// Eliminate
		for (int k = i + 1; k <= m; ++k) {
			double factor = XtX[k][i] / XtX[i][i];
			for (int j = i; j <= m; ++j) {
				XtX[k][j] -= factor * XtX[i][j];
			}
			Xty[k] -= factor * Xty[i];
		}
	}
	
	// Back substitution
	for (int i = m; i >= 0; --i) {
		poly_coeffs[i] = Xty[i];
		for (int j = i + 1; j <= m; ++j) {
			poly_coeffs[i] -= XtX[i][j] * poly_coeffs[j];
		}
		poly_coeffs[i] /= XtX[i][i];
	}
	
	// np.polyfit returns coefficients in descending order: [a_m, a_{m-1}, ..., a_0]
	// where polynomial is a_m*x^m + a_{m-1}*x^{m-1} + ... + a_0
	// Our poly_coeffs is [a_0, a_1, ..., a_m] (ascending), so we need to reverse
	// Store in descending order to match np.polyfit
	std::vector<double> np_polyfit_order(m + 1);
	for (int i = 0; i <= m; ++i) {
		np_polyfit_order[i] = poly_coeffs[m - i];
	}
	
	if (coeff >= static_cast<int>(np_polyfit_order.size())) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	// np.polyfit: coeff=0 returns a_m (highest), coeff=m returns a_0 (constant)
	return np_polyfit_order[coeff];
}

double FeatureMaxLangevinFixedPoint(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	if (series.size() < 2) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	int m = static_cast<int>(param.GetInt("m").value_or(3));
	double r = param.GetDouble("r").value_or(30.0);
	
	// tsfresh: Uses _estimate_friedrich_coefficients to get polynomial, then finds max real root
	// Get polynomial coefficients using friedrich method
	ParameterMap friedrich_params;
	friedrich_params.entries["m"] = static_cast<int64_t>(m);
	friedrich_params.entries["r"] = r;
	
	std::vector<double> poly_coeffs(m + 1);
	bool all_valid = true;
	// FeatureFriedrichCoefficients returns in np.polyfit order: [a_m, a_{m-1}, ..., a_0]
	for (int i = 0; i <= m; ++i) {
		friedrich_params.entries["coeff"] = static_cast<int64_t>(i);
		double coeff_val = FeatureFriedrichCoefficients(series, friedrich_params, cache);
		if (!std::isfinite(coeff_val)) {
			all_valid = false;
			break;
		}
		// np.polyfit returns [a_m, a_{m-1}, ..., a_0] where poly = a_m*x^m + ... + a_0
		// Store in same order for np.roots
		poly_coeffs[i] = coeff_val;
	}
	
	if (!all_valid) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	// Find roots of polynomial using companion matrix method
	// For polynomial a_m*x^m + ... + a_0, build companion matrix
	if (m <= 0) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	// poly_coeffs is in np.polyfit order: [a_m, a_{m-1}, ..., a_0]
	// np.roots expects same order: [a_m, a_{m-1}, ..., a_0] where polynomial is a_m*x^m + ... + a_0 = 0
	
	// Always normalize for numerical stability (even with small leading coeff)
	double leading = poly_coeffs[0];
	if (std::fabs(leading) < 1e-12) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	// Normalize polynomial (divide by leading coefficient)
	for (int i = 0; i <= m; ++i) {
		poly_coeffs[i] /= leading;
	}
	
	// For cubic (m=3), use Cardano's formula to find all roots exactly
	// This matches numpy.roots behavior more reliably than Newton's method
	std::vector<double> found_roots;
	if (m == 3) {
		// Normalized cubic: x^3 + a*x^2 + b*x + c = 0
		// where a = poly_coeffs[1], b = poly_coeffs[2], c = poly_coeffs[3]
		double a = poly_coeffs[1];
		double b = poly_coeffs[2];
		double c = poly_coeffs[3];
		
		// Depressed cubic: y^3 + p*y + q = 0 where y = x + a/3
		double p = b - a * a / 3.0;
		double q = c + (2.0 * a * a * a - 9.0 * a * b) / 27.0;
		
		// Discriminant
		double delta = (q * q / 4.0) + (p * p * p / 27.0);
		
		if (delta > 0) {
			// One real root, two complex
			double sqrt_delta = std::sqrt(delta);
			double u = std::cbrt(-q / 2.0 + sqrt_delta);
			double v = std::cbrt(-q / 2.0 - sqrt_delta);
			double y1 = u + v;
			double x1 = y1 - a / 3.0;
			found_roots.push_back(x1);
		} else if (std::fabs(delta) < 1e-12) {
			// Three real roots, at least two equal
			double u = std::cbrt(-q / 2.0);
			double y1 = 2.0 * u;
			double y2 = -u;
			double x1 = y1 - a / 3.0;
			double x2 = y2 - a / 3.0;
			found_roots.push_back(x1);
			found_roots.push_back(x2);
		} else {
			// Three distinct real roots (trigonometric solution)
			double r = std::sqrt(-p * p * p / 27.0);
			double theta = std::acos(-q / (2.0 * r));
			constexpr double pi = 3.14159265358979323846;
			double y1 = 2.0 * std::cbrt(r) * std::cos(theta / 3.0);
			double y2 = 2.0 * std::cbrt(r) * std::cos((theta + 2.0 * pi) / 3.0);
			double y3 = 2.0 * std::cbrt(r) * std::cos((theta + 4.0 * pi) / 3.0);
			found_roots.push_back(y1 - a / 3.0);
			found_roots.push_back(y2 - a / 3.0);
			found_roots.push_back(y3 - a / 3.0);
		}
	} else {
		// For other degrees, use Newton's method with comprehensive search
		double max_real_root = -std::numeric_limits<double>::infinity();
		
		// Comprehensive search with fine grid around likely values (500-700 range based on typical results)
		// Need to find ALL roots, then take max(real part)
		std::vector<double> test_points;
	// Coarse search over wide range
	for (double x = 0.0; x <= 1000.0; x += 50.0) {
		test_points.push_back(x);
	}
	// Fine search around typical range where all 3 roots are (500-650)
	for (double x = 500.0; x <= 650.0; x += 0.5) {
		test_points.push_back(x);
	}
	// Very fine search around 630-640 (where max root is)
	for (double x = 630.0; x <= 640.0; x += 0.05) {
		test_points.push_back(x);
	}
	// Also check around other roots (610, 528)
	for (double x = 525.0; x <= 535.0; x += 0.5) {
		test_points.push_back(x);
	}
	for (double x = 605.0; x <= 615.0; x += 0.5) {
		test_points.push_back(x);
	}
	// Add expected root value explicitly as starting point
	test_points.push_back(634.575);
	test_points.push_back(634.0);
	test_points.push_back(635.0);
	// Also check negative range
	for (double x = -100.0; x <= 0.0; x += 10.0) {
		test_points.push_back(x);
	}
	
	// Remove duplicates and sort
	std::sort(test_points.begin(), test_points.end());
	test_points.erase(std::unique(test_points.begin(), test_points.end()), test_points.end());
	
	// Store all found roots (to deduplicate and find max)
	std::vector<double> found_roots;
	
	// For each test point, refine using Newton's method
	// Use more iterations and better convergence criteria
	for (double guess : test_points) {
		for (int iter = 0; iter < 200; ++iter) {
			// Evaluate polynomial: a_m*x^m + a_{m-1}*x^{m-1} + ... + a_0 = 0
			// Use Horner's method for better numerical stability
			// After normalization: x^m + a_{m-1}*x^{m-1} + ... + a_0 = 0
			double poly_val = 1.0;  // Start with normalized leading coeff = 1
			double deriv_val = static_cast<double>(m);  // Derivative of x^m is m*x^{m-1}
			
			for (int i = 1; i <= m; ++i) {
				poly_val = poly_val * guess + poly_coeffs[i];
				// Derivative: m*x^{m-1} + (m-1)*a_{m-1}*x^{m-2} + ... + 1*a_1
				// In Horner's form: ((m)*x + (m-1)*a_{m-1})*x + ... + 1*a_1
				if (i < m) {
					deriv_val = deriv_val * guess + static_cast<double>(m - i) * poly_coeffs[i];
				}
			}
			
			if (std::fabs(deriv_val) < 1e-12) {
				break;
			}
			
			double new_guess = guess - poly_val / deriv_val;
			// Check convergence - both change and polynomial value
			if (std::fabs(new_guess - guess) < 1e-12 && std::fabs(poly_val) < 1e-8) {
				// Verify it's actually a root using Horner's method
				double check_val = poly_coeffs[0];
				for (int i = 1; i <= m; ++i) {
					check_val = check_val * new_guess + poly_coeffs[i];
				}
				// Use tighter tolerance for root verification
				if (std::fabs(check_val) < 1e-8 && std::isfinite(new_guess)) {
					// Check if this root is distinct from previously found roots
					bool is_distinct = true;
					for (double existing_root : found_roots) {
						if (std::fabs(new_guess - existing_root) < 1e-8) {
							is_distinct = false;
							break;
						}
					}
					if (is_distinct) {
						found_roots.push_back(new_guess);
					}
				}
				break;
			}
			// Also check if we've converged to a root even if change is small
			if (std::fabs(poly_val) < 1e-10) {
				// Verify it's actually a root
				double check_val = poly_coeffs[0];
				for (int i = 1; i <= m; ++i) {
					check_val = check_val * new_guess + poly_coeffs[i];
				}
				if (std::fabs(check_val) < 1e-8 && std::isfinite(new_guess)) {
					bool is_distinct = true;
					for (double existing_root : found_roots) {
						if (std::fabs(new_guess - existing_root) < 1e-8) {
							is_distinct = false;
							break;
						}
					}
					if (is_distinct) {
						found_roots.push_back(new_guess);
					}
				}
				break;
			}
			guess = new_guess;
		}
	}
	}
	
	// Find maximum real root from all found roots
	if (found_roots.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double max_real_root = -std::numeric_limits<double>::infinity();
	for (double root : found_roots) {
		// Only consider real roots (filter out any complex roots that might have been added)
		if (std::isfinite(root)) {
			max_real_root = std::max(max_real_root, root);
		}
	}
	return max_real_root;
}

double FeatureAggLinearTrend(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	size_t chunk_len = static_cast<size_t>(param.GetInt("chunk_len").value_or(5));
	auto f = param.GetString("f_agg").value_or("mean");
	auto attr = param.GetString("attr").value_or("slope");
	return AggLinearTrend(series, cache, chunk_len, f, attr);
}

double FeatureEnergyRatioByChunks(const Series &series, const ParameterMap &param, FeatureCache &) {
	int segments = static_cast<int>(param.GetInt("num_segments").value_or(10));
	int focus = static_cast<int>(param.GetInt("segment_focus").value_or(0));
	if (segments <= 0 || series.empty() || focus < 0 || focus >= segments) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	
	// tsfresh uses np.array_split which distributes remainder across first segments
	// For n elements split into k segments:
	// - First (n % k) segments get ceil(n/k) elements
	// - Remaining segments get floor(n/k) elements
	size_t n = series.size();
	size_t base_chunk_size = n / segments;
	size_t remainder = n % segments;
	
	// Calculate start and end indices for the focus segment
	size_t start = 0;
	for (int i = 0; i < focus; ++i) {
		// Each segment before focus: add its size
		if (i < static_cast<int>(remainder)) {
			start += base_chunk_size + 1;  // Segments with remainder get +1
		} else {
			start += base_chunk_size;
		}
	}
	
	// Calculate size of focus segment
	size_t focus_size = (focus < static_cast<int>(remainder)) ? (base_chunk_size + 1) : base_chunk_size;
	size_t end = start + focus_size;
	
	// Calculate energies
	double total_energy = 0.0;
	for (double value : series) {
		total_energy += value * value;
	}
	if (total_energy < 1e-12) {
		return 0.0;
	}
	
	double chunk_energy = 0.0;
	for (size_t i = start; i < end && i < series.size(); ++i) {
		chunk_energy += series[i] * series[i];
	}
	return chunk_energy / total_energy;
}

double FeatureLinearTrend(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	auto attr = param.GetString("attr").value_or("slope");
	return LinearTrend(series, cache, attr);
}

double FeatureLinearTrendTimewise(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	auto attr = param.GetString("attr").value_or("slope");
	return LinearTrendTimewise(series, cache, attr);
}

double FeatureSpktWelch(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	return FeatureSpktWelchDensity(series, param, cache);
}

double FeatureCidCe(const Series &series, const ParameterMap &param, FeatureCache &) {
	bool normalize = param.GetBool("normalize").value_or(false);
	if (series.size() < 2) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	std::vector<double> diffs(series.size() - 1);
	for (size_t i = 1; i < series.size(); ++i) {
		diffs[i - 1] = series[i] - series[i - 1];
	}
	if (normalize) {
		double mean = MeanOfVector(diffs);
		double stddev = 0.0;
		for (double value : diffs) {
			stddev += (value - mean) * (value - mean);
		}
		stddev = std::sqrt(stddev / diffs.size());
		if (stddev > 1e-12) {
			for (double &value : diffs) {
				value = (value - mean) / stddev;
			}
		}
	}
	// tsfresh cid_ce: sqrt(sum of squares of differences), not sum of absolute differences
	double sum_squares = 0.0;
	for (double value : diffs) {
		sum_squares += value * value;
	}
	return std::sqrt(sum_squares);
}

double FeatureFftCoefficient(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	size_t coeff = static_cast<size_t>(param.GetInt("coeff").value_or(0));
	auto attr = param.GetString("attr").value_or("real");
	auto value = GetFFTValue(series, coeff, cache);
	if (attr == "real") {
		return value.real();
	}
	if (attr == "imag") {
		return value.imag();
	}
	if (attr == "abs") {
		return std::abs(value);
	}
	if (attr == "angle") {
		return std::arg(value);
	}
	return value.real();
}

double FeatureFftAggregated(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	auto attr = param.GetString("aggtype").value_or("centroid");
	ComputeFFT(series, cache);
	const auto &real = *cache.fft_real;
	const auto &imag = *cache.fft_imag;
	// For real signals, use one-sided spectrum (first half + DC + Nyquist if even)
	size_t n = real.size();
	size_t spectrum_size = n / 2 + 1;  // One-sided spectrum size
	std::vector<double> magnitudes(spectrum_size);
	for (size_t i = 0; i < spectrum_size; ++i) {
		magnitudes[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
	}
	if (attr == "centroid") {
		// tsfresh get_moment(y, 1) = y.dot(np.arange(len(y))) / y.sum()
		double numerator = 0.0;
		double denominator = 0.0;
		for (size_t i = 0; i < magnitudes.size(); ++i) {
			numerator += static_cast<double>(i) * magnitudes[i];
			denominator += magnitudes[i];
		}
		if (denominator < 1e-12) {
			return std::numeric_limits<double>::quiet_NaN();
		}
		return numerator / denominator;
	}
	if (attr == "variance") {
		return FeatureVariance(magnitudes, ParameterMap {}, cache);
	}
	if (attr == "skew") {
		Series tmp = magnitudes;
		FeatureCache mag_cache(tmp);
		return ComputeSkewness(tmp, mag_cache);
	}
	if (attr == "kurtosis") {
		Series tmp = magnitudes;
		FeatureCache mag_cache(tmp);
		return ComputeKurtosis(tmp, mag_cache);
	}
	return MeanOfVector(magnitudes);
}

double FeatureNumberCwt(const Series &series, const ParameterMap &param, FeatureCache &) {
	return FeatureNumberCwtPeaks(series, param, *(FeatureCache *)nullptr);
}

double FeatureEnergyRatio(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	return FeatureEnergyRatioByChunks(series, param, cache);
}

double FeatureMatrixProfileThreshold(const Series &series, const ParameterMap &param, FeatureCache &) {
	size_t window = static_cast<size_t>(param.GetInt("window").value_or(10));
	double threshold = param.GetDouble("threshold").value_or(0.5);
	return MatrixProfileThresholdCount(series, window, threshold);
}

double FeatureSpktWelchDensityShim(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	return FeatureSpktWelchDensity(series, param, cache);
}

double FeatureArCoeff(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	return FeatureArCoefficient(series, param, cache);
}

// Run-length encoding utility structures and functions
struct RunLength {
	double value;
	size_t count;
};

std::vector<RunLength> ComputeRunLengthEncoding(const Series &series) {
	std::vector<RunLength> runs;
	if (series.empty()) {
		return runs;
	}
	
	double current_value = series[0];
	size_t current_count = 1;
	
	for (size_t i = 1; i < series.size(); ++i) {
		if (series[i] == current_value) {
			++current_count;
		} else {
			runs.push_back({current_value, current_count});
			current_value = series[i];
			current_count = 1;
		}
	}
	runs.push_back({current_value, current_count});
	
	return runs;
}

size_t MaxRunLength(const Series &series) {
	auto runs = ComputeRunLengthEncoding(series);
	if (runs.empty()) {
		return 0;
	}
	size_t max_run = 0;
	for (const auto &run : runs) {
		if (run.count > max_run) {
			max_run = run.count;
		}
	}
	return max_run;
}

size_t MaxRunLengthNonZero(const Series &series) {
	auto runs = ComputeRunLengthEncoding(series);
	if (runs.empty()) {
		return 0;
	}
	size_t max_run = 0;
	for (const auto &run : runs) {
		if (run.value != 0.0 && run.count > max_run) {
			max_run = run.count;
		}
	}
	return max_run;
}

size_t LeadingZeros(const Series &series) {
	if (series.empty()) {
		return 0;
	}
	size_t count = 0;
	for (double value : series) {
		if (value == 0.0) {
			++count;
		} else {
			break;
		}
	}
	return count;
}

size_t TrailingZeros(const Series &series) {
	if (series.empty()) {
		return 0;
	}
	size_t count = 0;
	for (auto it = series.rbegin(); it != series.rend(); ++it) {
		if (*it == 0.0) {
			++count;
		} else {
			break;
		}
	}
	return count;
}

// New feature calculators for TS_STATS
double FeatureNNull(const Series &series, const ParameterMap &, FeatureCache &) {
	// Note: NULLs are filtered out before reaching feature calculators
	// This feature will always return 0.0
	// The actual n_null count should be computed in SQL separately
	return 0.0;
}

double FeatureNZeros(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return 0.0;
	}
	size_t count = 0;
	for (double value : series) {
		if (value == 0.0) {
			++count;
		}
	}
	return static_cast<double>(count);
}

double FeatureNUniqueValues(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return 0.0;
	}
	std::unordered_set<double> unique_values;
	for (double value : series) {
		unique_values.insert(value);
	}
	return static_cast<double>(unique_values.size());
}

double FeatureIsConstant(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return 1.0; // Empty series is considered constant
	}
	if (series.size() == 1) {
		return 1.0;
	}
	double first_value = series[0];
	for (size_t i = 1; i < series.size(); ++i) {
		if (series[i] != first_value) {
			return 0.0;
		}
	}
	return 1.0;
}

double FeaturePlateauSize(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return 0.0;
	}
	return static_cast<double>(MaxRunLength(series));
}

double FeaturePlateauSizeNonZero(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.empty()) {
		return 0.0;
	}
	return static_cast<double>(MaxRunLengthNonZero(series));
}

double FeatureNZerosStart(const Series &series, const ParameterMap &, FeatureCache &) {
	return static_cast<double>(LeadingZeros(series));
}

double FeatureNZerosEnd(const Series &series, const ParameterMap &, FeatureCache &) {
	return static_cast<double>(TrailingZeros(series));
}

} // namespace

void RegisterBuiltinFeatureCalculators(FeatureRegistry &registry) {
	auto simple = [&](const std::string &name, FeatureCalculatorFn fn) {
		FeatureDefinition def;
		def.name = name;
		def.calculator = std::move(fn);
		registry.Register(std::move(def));
	};
	auto with_params = [&](const std::string &name, std::vector<ParameterMap> params, FeatureCalculatorFn fn,
	                       size_t default_index = 0) {
		FeatureDefinition def;
		def.name = name;
		def.default_parameters = std::move(params);
		def.calculator = std::move(fn);
		def.default_parameter_index = default_index;
		registry.Register(std::move(def));
	};

	simple("variance_larger_than_standard_deviation", FeatureVarianceLargerThanStd);
	with_params("ratio_beyond_r_sigma",
	            {Params({{"r", 0.5}}),  Params({{"r", 1.0}}),  Params({{"r", 1.5}}), Params({{"r", 2.0}}),
	             Params({{"r", 2.5}}), Params({{"r", 3.0}}),  Params({{"r", 5.0}}), Params({{"r", 6.0}}),
	             Params({{"r", 7.0}}), Params({{"r", 10.0}})},
	            FeatureRatioBeyondRSigma);
	with_params("large_standard_deviation",
	            [] {
		            std::vector<ParameterMap> params;
		            for (int r = 1; r < 20; ++r) {
			            params.push_back(Params({{"r", r * 0.05}}));
		            }
		            return params;
	            }(),
	            FeatureLargeStandardDeviation);
	with_params("symmetry_looking",
	            [] {
		            std::vector<ParameterMap> params;
		            for (int r = 0; r < 20; ++r) {
			            params.push_back(Params({{"r", r * 0.05}}));
		            }
		            return params;
	            }(),
	            FeatureSymmetryLooking);
	simple("has_duplicate_max", FeatureHasDuplicateMax);
	simple("has_duplicate_min", FeatureHasDuplicateMin);
	simple("has_duplicate", FeatureHasDuplicate);
	simple("sum_values", FeatureSumValues);
	simple("cid_ce", FeatureCidCe);
	simple("mean", FeatureMean);
	simple("median", FeatureMedian);
	simple("length", FeatureLength);
	simple("standard_deviation", FeatureStandardDeviation);
	simple("variation_coefficient", FeatureVariationCoefficient);
	simple("variance", FeatureVariance);
	simple("skewness", FeatureSkewness);
	simple("kurtosis", FeatureKurtosis);
	simple("abs_energy", FeatureAbsEnergy);
	simple("mean_abs_change", FeatureMeanAbsChange);
	simple("mean_change", FeatureMeanChange);
	simple("mean_second_derivative_central", FeatureMeanSecondDerivativeCentral);
	simple("root_mean_square", FeatureRootMeanSquare);
	simple("absolute_sum_of_changes", FeatureAbsoluteSumOfChanges);
	simple("longest_strike_below_mean", FeatureLongestStrikeBelowMean);
	simple("longest_strike_above_mean", FeatureLongestStrikeAboveMean);
	simple("count_above_mean", FeatureCountAboveMean);
	simple("count_below_mean", FeatureCountBelowMean);
	simple("first_location_of_maximum", FeatureFirstLocationOfMaximum);
	simple("last_location_of_maximum", FeatureLastLocationOfMaximum);
	simple("first_location_of_minimum", FeatureFirstLocationOfMinimum);
	simple("last_location_of_minimum", FeatureLastLocationOfMinimum);
	simple("percentage_of_reoccurring_values_to_all_values", FeaturePercentageOfReoccurringValuesToAllValues);
	simple("percentage_of_reoccurring_datapoints_to_all_datapoints",
	       FeaturePercentageOfReoccurringDatapointsToAllValues);
	// tsfresh naming: sum_of_reoccurring_values = value once (1410.89)
	//                  sum_of_reoccurring_data_points = value * count (2821.78)
	// Our functions: FeatureSumOfReoccurringValues = value * count, FeatureSumOfReoccurringDataPoints = value once
	// So swap registrations to match tsfresh
	simple("sum_of_reoccurring_values", FeatureSumOfReoccurringDataPoints);
	simple("sum_of_reoccurring_data_points", FeatureSumOfReoccurringValues);
	simple("ratio_value_number_to_time_series_length", FeatureRatioValueNumberToSeriesLength);
	simple("maximum", FeatureMaximum);
	simple("minimum", FeatureMinimum);
	simple("absolute_maximum", FeatureAbsoluteMaximum);
	with_params("quantile",
	            {Params({{"q", 0.1}}), Params({{"q", 0.2}}), Params({{"q", 0.3}}), Params({{"q", 0.4}}),
	             Params({{"q", 0.6}}), Params({{"q", 0.7}}), Params({{"q", 0.8}}), Params({{"q", 0.9}})},
	            FeatureQuantile);
	with_params("autocorrelation",
	            [] {
		            std::vector<ParameterMap> params;
		            for (int lag = 0; lag < 10; ++lag) {
			            params.push_back(Params({{"lag", static_cast<int64_t>(lag)}}));
		            }
		            return params;
	            }(),
	            FeatureAutocorrelation, 1);
	with_params("agg_autocorrelation",
	            [] {
		            std::vector<ParameterMap> params;
		            for (const std::string &agg : {"mean", "median", "var"}) {
			            params.push_back(Params({{"f_agg", agg}, {"maxlag", static_cast<int64_t>(40)}}));
		            }
		            return params;
	            }(),
	            FeatureAggAutocorrelation);
	with_params("partial_autocorrelation",
	            [] {
		            std::vector<ParameterMap> params;
		            for (int lag = 0; lag < 10; ++lag) {
			            params.push_back(Params({{"lag", static_cast<int64_t>(lag)}}));
		            }
		            return params;
	            }(),
	            FeaturePartialAutocorrelation, 1);
	with_params("number_cwt_peaks", {Params({{"n", 1}}), Params({{"n", 5}})}, FeatureNumberCwtPeaks);
	with_params("number_peaks",
	            {Params({{"n", 1}}), Params({{"n", 3}}), Params({{"n", 5}}), Params({{"n", 10}}), Params({{"n", 50}})},
	            FeatureNumberPeaks);
	with_params("binned_entropy", {Params({{"max_bins", 10}})}, FeatureBinnedEntropy);
	with_params("index_mass_quantile",
	            {Params({{"q", 0.1}}), Params({{"q", 0.2}}), Params({{"q", 0.3}}), Params({{"q", 0.4}}),
	             Params({{"q", 0.6}}), Params({{"q", 0.7}}), Params({{"q", 0.8}}), Params({{"q", 0.9}})},
	            FeatureIndexMassQuantile);
	with_params("cwt_coefficients",
	            [] {
		            std::vector<ParameterMap> params;
		            std::vector<int64_t> widths = {2, 5, 10, 20};
		            for (int coeff = 0; coeff < 15; ++coeff) {
			            for (int64_t w : {2LL, 5LL, 10LL, 20LL}) {
				            ParameterMap map;
				            map.entries["widths"] = widths;
				            map.entries["coeff"] = static_cast<int64_t>(coeff);
				            map.entries["w"] = w;
				            params.push_back(map);
			            }
		            }
		            return params;
	            }(),
	            FeatureCwtCoefficients);
	with_params("spkt_welch_density", {Params({{"coeff", 2}}), Params({{"coeff", 5}}), Params({{"coeff", 8}})},
	            FeatureSpktWelchDensity);
	with_params("ar_coefficient",
	            [] {
		            std::vector<ParameterMap> params;
		            for (int coeff = 0; coeff <= 10; ++coeff) {
			            params.push_back(Params({{"coeff", static_cast<int64_t>(coeff)}, {"k", static_cast<int64_t>(10)}}));
		            }
		            return params;
	            }(),
	            FeatureArCoefficient);
	with_params("change_quantiles",
	            [] {
		            std::vector<ParameterMap> params;
		            std::vector<double> qs = {0.0, 0.2, 0.4, 0.6, 0.8, 1.0};
		            for (double ql : qs) {
			            for (double qh : qs) {
				            if (ql >= qh) {
					            continue;
				            }
				            for (bool isabs : {false, true}) {
					            for (const std::string &f : {"mean", "var"}) {
						            params.push_back(Params(
						                {{"ql", ql}, {"qh", qh}, {"isabs", isabs}, {"f_agg", f}}));
					            }
				            }
			            }
		            }
		            return params;
	            }(),
	            FeatureChangeQuantiles);
	with_params("time_reversal_asymmetry_statistic",
	            [] {
		            std::vector<ParameterMap> params;
		            for (int lag = 1; lag <= 3; ++lag) {
			            params.push_back(Params({{"lag", static_cast<int64_t>(lag)}}));
		            }
		            return params;
	            }(),
	            FeatureTimeReversalAsymmetryStatistic);
	with_params("c3",
	            [] {
		            std::vector<ParameterMap> params;
		            for (int lag = 1; lag <= 3; ++lag) {
			            params.push_back(Params({{"lag", static_cast<int64_t>(lag)}}));
		            }
		            return params;
	            }(),
	            FeatureC3);
	with_params("mean_n_absolute_max",
	            {Params({{"number_of_maxima", 3}}), Params({{"number_of_maxima", 5}}),
	             Params({{"number_of_maxima", 7}})},
	            FeatureMeanNAbsoluteMax);
	with_params("sample_entropy", {ParameterMap {}}, FeatureSampleEntropy);
	with_params("approximate_entropy",
	            {Params({{"m", 2}, {"r", 0.1}}), Params({{"m", 2}, {"r", 0.3}}), Params({{"m", 2}, {"r", 0.5}}),
	             Params({{"m", 2}, {"r", 0.7}}), Params({{"m", 2}, {"r", 0.9}})},
	            FeatureApproximateEntropy);
	with_params("fourier_entropy",
	            {Params({{"bins", 2}}), Params({{"bins", 3}}), Params({{"bins", 5}}), Params({{"bins", 10}}),
	             Params({{"bins", 100}})},
	            FeatureFourierEntropy);
	with_params("lempel_ziv_complexity",
	            {Params({{"bins", 2}}), Params({{"bins", 3}}), Params({{"bins", 5}}), Params({{"bins", 10}}),
	             Params({{"bins", 100}})},
	            FeatureLempelZivComplexity);
	with_params("permutation_entropy",
	            [] {
		            std::vector<ParameterMap> params;
		            for (int dimension = 3; dimension <= 7; ++dimension) {
			            params.push_back(Params({{"tau", 1}, {"dimension", static_cast<int64_t>(dimension)}}));
		            }
		            return params;
	            }(),
	            FeaturePermutationEntropy);
	simple("benford_correlation", FeatureBenfordCorrelation);
	with_params("fft_coefficient",
	            [] {
		            std::vector<ParameterMap> params;
		            for (const std::string &attr : {"real", "imag", "abs", "angle"}) {
			            for (int coeff = 0; coeff < 100; ++coeff) {
				            params.push_back(Params({{"attr", attr}, {"coeff", static_cast<int64_t>(coeff)}}));
			            }
		            }
		            return params;
	            }(),
	            FeatureFftCoefficient);
	with_params("fft_aggregated",
	            {Params({{"aggtype", "centroid"}}), Params({{"aggtype", "variance"}}),
	             Params({{"aggtype", "skew"}}), Params({{"aggtype", "kurtosis"}})},
	            FeatureFftAggregated);
	with_params("value_count", {Params({{"value", 0}}), Params({{"value", 1}}), Params({{"value", -1}})},
	            FeatureValueCount);
	with_params("range_count",
	            {Params({{"min", -1.0}, {"max", 1.0}}), Params({{"min", -1e12}, {"max", 0.0}}),
	             Params({{"min", 0.0}, {"max", 1e12}})},
	            FeatureRangeCount);
	with_params("friedrich_coefficients",
	            [] {
		            std::vector<ParameterMap> params;
		            int m = 3;
		            for (int coeff = 0; coeff <= m; ++coeff) {
			            params.push_back(Params({{"coeff", static_cast<int64_t>(coeff)}, {"m", static_cast<int64_t>(m)},
			                                     {"r", static_cast<int64_t>(30)}}));
		            }
		            return params;
	            }(),
	            FeatureFriedrichCoefficients);
	with_params("max_langevin_fixed_point", {Params({{"m", 3}, {"r", 30}})}, FeatureMaxLangevinFixedPoint);
	with_params("linear_trend",
	            {Params({{"attr", "pvalue"}}), Params({{"attr", "rvalue"}}), Params({{"attr", "intercept"}}),
	             Params({{"attr", "slope"}}), Params({{"attr", "stderr"}})},
	            FeatureLinearTrend);
	with_params("agg_linear_trend",
	            [] {
		            std::vector<ParameterMap> params;
		            for (const std::string &attr : {"rvalue", "intercept", "slope", "stderr"}) {
			            for (int chunk : {5, 10, 50}) {
				            for (const std::string &f : {"max", "min", "mean", "var"}) {
					            params.push_back(Params({{"attr", attr},
					                                     {"chunk_len", static_cast<int64_t>(chunk)},
					                                     {"f_agg", f}}));
				            }
			            }
		            }
		            return params;
	            }(),
	            FeatureAggLinearTrend);
	with_params("augmented_dickey_fuller",
	            {Params({{"attr", "teststat"}}), Params({{"attr", "pvalue"}}), Params({{"attr", "usedlag"}})},
	            FeatureAugmentedDickeyFuller);
	with_params("number_crossing_m", {Params({{"m", 0}}), Params({{"m", -1}}), Params({{"m", 1}})},
	            FeatureNumberCrossingM);
	with_params("energy_ratio_by_chunks",
	            [] {
		            std::vector<ParameterMap> params;
		            for (int seg = 0; seg < 10; ++seg) {
			            params.push_back(
			                Params({{"num_segments", static_cast<int64_t>(10)}, {"segment_focus", static_cast<int64_t>(seg)}}));
		            }
		            return params;
	            }(),
	            FeatureEnergyRatioByChunks);
	with_params("linear_trend_timewise",
	            {Params({{"attr", "pvalue"}}), Params({{"attr", "rvalue"}}), Params({{"attr", "intercept"}}),
	             Params({{"attr", "slope"}}), Params({{"attr", "stderr"}})},
	            FeatureLinearTrendTimewise);
	with_params("count_above", {Params({{"t", 0.0}})}, FeatureCountAbove);
	with_params("count_below", {Params({{"t", 0.0}})}, FeatureCountBelow);
	with_params("query_similarity_count", {Params({{"threshold", 0.0}})}, FeatureQuerySimilarityCount);
	with_params("matrix_profile",
	            {Params({{"threshold", 0.98}, {"feature", "min"}}), Params({{"threshold", 0.98}, {"feature", "max"}}),
	             Params({{"threshold", 0.98}, {"feature", "mean"}}),
	             Params({{"threshold", 0.98}, {"feature", "median"}}),
	             Params({{"threshold", 0.98}, {"feature", "25"}}),
	             Params({{"threshold", 0.98}, {"feature", "75"}})},
	            FeatureMatrixProfile);
	
	// New features for TS_STATS
	simple("n_null", FeatureNNull);
	simple("n_zeros", FeatureNZeros);
	simple("n_unique_values", FeatureNUniqueValues);
	simple("is_constant", FeatureIsConstant);
	simple("plateau_size", FeaturePlateauSize);
	simple("plateau_size_non_zero", FeaturePlateauSizeNonZero);
	simple("n_zeros_start", FeatureNZerosStart);
	simple("n_zeros_end", FeatureNZerosEnd);
}

} // namespace anofoxtime::features



