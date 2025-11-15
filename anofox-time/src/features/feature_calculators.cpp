#include "anofox-time/features/feature_calculators.hpp"
#include "anofox-time/features/feature_math.hpp"
#include "anofox-time/features/feature_types.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>

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

double FeatureMeanSecondDerivativeCentral(const Series &series, const ParameterMap &, FeatureCache &cache) {
	auto second = ComputeSecondDiffs(series, cache);
	if (second.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double sum = std::accumulate(second.begin(), second.end(), 0.0);
	return sum / second.size();
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
			return static_cast<double>(i - 1) / series.size();
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
			return static_cast<double>(i - 1) / series.size();
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
			reoccurring += kv.second;
		}
	}
	return static_cast<double>(reoccurring) / series.size();
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
			++reoccurring;
		}
	}
	return static_cast<double>(reoccurring) / counts.size();
}

double FeatureSumOfReoccurringValues(const Series &series, const ParameterMap &, FeatureCache &) {
	std::unordered_map<double, size_t> counts;
	for (double value : series) {
		counts[value]++;
	}
	double sum = 0.0;
	for (const auto &kv : counts) {
		if (kv.second > 1) {
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
	return static_cast<double>(count);
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
	for (size_t i = 1; i < series.size(); ++i) {
		if ((series[i - 1] - m) * (series[i] - m) < 0) {
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

double FeatureAugmentedDickeyFuller(const Series &series, const ParameterMap &param, FeatureCache &) {
	if (series.size() < 5) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	size_t lag = 1;
	size_t n = series.size();
	std::vector<double> diff(n - 1);
	for (size_t i = 1; i < n; ++i) {
		diff[i - 1] = series[i] - series[i - 1];
	}
	double sum_y = 0.0;
	for (size_t i = lag; i < n - 1; ++i) {
		sum_y += series[i];
	}
	double mean_y = sum_y / (n - 1 - lag);
	double sum_diff = 0.0;
	for (double value : diff) {
		sum_diff += value;
	}
	double mean_diff = sum_diff / diff.size();
	double numerator = 0.0;
	double denominator = 0.0;
	for (size_t i = lag; i < n - 1; ++i) {
		double y = series[i] - mean_y;
		double dy = diff[i] - mean_diff;
		numerator += y * dy;
		denominator += y * y;
	}
	if (denominator < 1e-12) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double beta = numerator / denominator;
	double teststat = beta;
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
	if (series.size() < 3) {
		return 0.0;
	}
	size_t count = 0;
	for (size_t i = 1; i + 1 < series.size(); ++i) {
		if (series[i] > series[i - 1] && series[i] > series[i + 1]) {
			++count;
			if (static_cast<int>(count) >= n) {
				break;
			}
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
			return static_cast<double>(i) / series.size();
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
	std::vector<double> phi(order + 1, 0.0);
	std::vector<double> pacf(order + 1, 0.0);
	for (size_t k = 1; k <= order; ++k) {
		double num = 0.0;
		double den = 0.0;
		for (size_t t = k; t < series.size(); ++t) {
			num += series[t] * series[t - k];
			den += series[t - k] * series[t - k];
		}
		if (std::fabs(den) < 1e-12) {
			break;
		}
		phi[k] = num / den;
		pacf[k] = phi[k];
		for (size_t j = 1; j < k; ++j) {
			phi[j] = phi[j] - pacf[k] * phi[k - j];
		}
	}
	if (coeff >= pacf.size()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return pacf[coeff];
}

double FeatureChangeQuantiles(const Series &series, const ParameterMap &param, FeatureCache &cache) {
	double ql = param.GetDouble("ql").value_or(0.0);
	double qh = param.GetDouble("qh").value_or(1.0);
	bool isabs = param.GetBool("isabs").value_or(false);
	auto f = param.GetString("f_agg").value_or("mean");
	if (ql >= qh) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double low = ComputeQuantile(series, ql, cache);
	double high = ComputeQuantile(series, qh, cache);
	if (std::fabs(high - low) < 1e-12) {
		return 0.0;
	}
	std::vector<double> diffs;
	for (double value : series) {
		if (value >= low && value <= high) {
			diffs.push_back(value);
		}
	}
	if (diffs.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	if (isabs) {
		for (double &value : diffs) {
			value = std::fabs(value);
		}
	}
	if (f == "mean") {
		return MeanOfVector(diffs);
	}
	if (f == "var") {
		double mean = MeanOfVector(diffs);
		double accum = 0.0;
		for (double value : diffs) {
			double diff = value - mean;
			accum += diff * diff;
		}
		return accum / diffs.size();
	}
	return MeanOfVector(diffs);
}

double FeatureTimeReversalAsymmetryStatistic(const Series &series, const ParameterMap &param, FeatureCache &) {
	int lag = static_cast<int>(param.GetInt("lag").value_or(1));
	if (lag <= 0 || series.size() <= static_cast<size_t>(lag * 2)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double sum = 0.0;
	size_t count = 0;
	for (size_t i = 2 * lag; i < series.size(); ++i) {
		double value = (series[i] - series[i - lag]) * (series[i - lag] - series[i - 2 * lag]);
		sum += value;
		++count;
	}
	if (count == 0) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return sum / count;
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

double FeatureFriedrichCoefficients(const Series &series, const ParameterMap &param, FeatureCache &) {
	int m = static_cast<int>(param.GetInt("m").value_or(3));
	int coeff = static_cast<int>(param.GetInt("coeff").value_or(0));
	if (series.size() < 2 || coeff < 0 || coeff > m) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	// Estimate by fitting polynomial drift to velocity field
	std::vector<double> x(series.size());
	for (size_t i = 0; i < series.size(); ++i) {
		x[i] = static_cast<double>(i);
	}
	std::vector<double> y(series.size() - 1);
	for (size_t i = 1; i < series.size(); ++i) {
		y[i - 1] = series[i] - series[i - 1];
	}
	std::vector<double> X(series.size() - 1);
	for (size_t i = 1; i < series.size(); ++i) {
		X[i - 1] = series[i - 1];
	}
	std::vector<double> coeffs(m + 1, 0.0);
	for (int k = 0; k <= m; ++k) {
		double numerator = 0.0;
		double denominator = 0.0;
		for (size_t i = 0; i < X.size(); ++i) {
			double basis = std::pow(X[i], k);
			numerator += basis * y[i];
			denominator += basis * basis;
		}
		if (denominator < 1e-12) {
			continue;
		}
		coeffs[k] = numerator / denominator;
	}
	if (coeff >= static_cast<int>(coeffs.size())) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return coeffs[coeff];
}

double FeatureMaxLangevinFixedPoint(const Series &series, const ParameterMap &, FeatureCache &) {
	if (series.size() < 2) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	std::vector<double> velocities(series.size() - 1);
	for (size_t i = 1; i < series.size(); ++i) {
		velocities[i - 1] = series[i] - series[i - 1];
	}
	auto minmax = std::minmax_element(series.begin(), series.end());
	double max_abs = std::max(std::fabs(*minmax.first), std::fabs(*minmax.second));
	if (max_abs < 1e-12) {
		return 0.0;
	}
	return max_abs;
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
	if (segments <= 0 || series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	size_t chunk_size = std::max<size_t>(1, series.size() / segments);
	double total_energy = 0.0;
	for (double value : series) {
		total_energy += value * value;
	}
	if (total_energy < 1e-12) {
		return 0.0;
	}
	size_t start = std::min<size_t>(focus * chunk_size, series.size());
	size_t end = std::min(start + chunk_size, series.size());
	double chunk_energy = 0.0;
	for (size_t i = start; i < end; ++i) {
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
	double energy = 0.0;
	for (double value : diffs) {
		energy += std::sqrt(value * value);
	}
	return energy;
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
	std::vector<double> magnitudes(real.size());
	for (size_t i = 0; i < real.size(); ++i) {
		magnitudes[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
	}
	if (attr == "centroid") {
		double numerator = 0.0;
		double denominator = 0.0;
		for (size_t i = 0; i < magnitudes.size(); ++i) {
			numerator += i * magnitudes[i];
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
	simple("sum_of_reoccurring_values", FeatureSumOfReoccurringValues);
	simple("sum_of_reoccurring_data_points", FeatureSumOfReoccurringDataPoints);
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
}

} // namespace anofoxtime::features

