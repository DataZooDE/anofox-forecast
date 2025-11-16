#include "anofox-time/features/feature_math.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <numeric>
#include <queue>
#include <random>
#include <stdexcept>
#include <unordered_map>

namespace anofoxtime::features {

namespace {

constexpr double kEpsilon = 1e-12;
constexpr double kPi = 3.14159265358979323846;

template <typename T>
double MeanOfVector(const std::vector<T> &values) {
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

} // namespace

double ComputeMean(const Series &series, FeatureCache &cache) {
	if (cache.mean) {
		return *cache.mean;
	}
	if (series.empty()) {
		cache.mean = std::numeric_limits<double>::quiet_NaN();
		return *cache.mean;
	}
	double sum = std::accumulate(series.begin(), series.end(), 0.0);
	cache.mean = sum / static_cast<double>(series.size());
	return *cache.mean;
}

double ComputeVariance(const Series &series, FeatureCache &cache) {
	if (cache.variance) {
		return *cache.variance;
	}
	if (series.empty()) {
		cache.variance = std::numeric_limits<double>::quiet_NaN();
		return *cache.variance;
	}
	double mean = ComputeMean(series, cache);
	double accum = 0.0;
	for (double value : series) {
		double diff = value - mean;
		accum += diff * diff;
	}
	cache.variance = accum / static_cast<double>(series.size());
	return *cache.variance;
}

double ComputeStdDev(const Series &series, FeatureCache &cache) {
	if (cache.stddev) {
		return *cache.stddev;
	}
	double variance = ComputeVariance(series, cache);
	cache.stddev = variance < 0 ? std::numeric_limits<double>::quiet_NaN() : std::sqrt(variance);
	return *cache.stddev;
}

std::vector<double> ComputeSorted(const Series &series, FeatureCache &cache) {
	if (!cache.sorted_values) {
		cache.sorted_values = series;
		std::sort(cache.sorted_values->begin(), cache.sorted_values->end());
	}
	return *cache.sorted_values;
}

double ComputeMedian(const Series &series, FeatureCache &cache) {
	if (cache.median) {
		return *cache.median;
	}
	if (series.empty()) {
		cache.median = std::numeric_limits<double>::quiet_NaN();
		return *cache.median;
	}
	auto sorted = ComputeSorted(series, cache);
	size_t n = sorted.size();
	if (n % 2 == 1) {
		cache.median = sorted[n / 2];
	} else {
		cache.median = (sorted[n / 2 - 1] + sorted[n / 2]) / 2.0;
	}
	return *cache.median;
}

std::vector<double> ComputeAbsSorted(const Series &series, FeatureCache &cache) {
	if (!cache.abs_sorted_values) {
		cache.abs_sorted_values = std::vector<double>(series.size());
		std::transform(series.begin(), series.end(), cache.abs_sorted_values->begin(),
		               [](double v) { return std::fabs(v); });
		std::sort(cache.abs_sorted_values->begin(), cache.abs_sorted_values->end());
	}
	return *cache.abs_sorted_values;
}

double ComputeSkewness(const Series &series, FeatureCache &cache) {
	if (series.size() < 3) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double mean = ComputeMean(series, cache);
	double variance = ComputeVariance(series, cache);
	if (variance < kEpsilon) {
		return 0.0;
	}
	double stddev = std::sqrt(variance);
	double accumulator = 0.0;
	for (double value : series) {
		accumulator += std::pow((value - mean) / stddev, 3);
	}
	double n = static_cast<double>(series.size());
	return (n / ((n - 1.0) * (n - 2.0))) * accumulator;
}

double ComputeKurtosis(const Series &series, FeatureCache &cache) {
	if (series.size() < 4) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double mean = ComputeMean(series, cache);
	double variance = ComputeVariance(series, cache);
	if (variance < kEpsilon) {
		return 0.0;
	}
	double stddev2 = variance * variance;
	double accumulator = 0.0;
	for (double value : series) {
		double diff = value - mean;
		accumulator += diff * diff * diff * diff;
	}
	double n = static_cast<double>(series.size());
	double kurtosis = (n * (n + 1) * accumulator - 3.0 * std::pow(n - 1.0, 2) * std::pow(variance, 2)) /
	                  ((n - 1.0) * (n - 2.0) * (n - 3.0) * stddev2);
	return kurtosis;
}

double ComputeQuantile(const Series &series, double q, FeatureCache &cache) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	auto sorted = ComputeSorted(series, cache);
	double pos = q * (sorted.size() - 1);
	size_t idx = static_cast<size_t>(pos);
	double frac = pos - idx;
	if (idx + 1 < sorted.size()) {
		return sorted[idx] * (1.0 - frac) + sorted[idx + 1] * frac;
	}
	return sorted.back();
}

std::vector<double> ComputeDiffs(const Series &series, FeatureCache &cache) {
	if (!cache.diffs) {
		std::vector<double> diffs;
		if (series.size() >= 2) {
			diffs.reserve(series.size() - 1);
			for (size_t i = 1; i < series.size(); ++i) {
				diffs.push_back(series[i] - series[i - 1]);
			}
		}
		cache.diffs = std::move(diffs);
	}
	return *cache.diffs;
}

std::vector<double> ComputeSecondDiffs(const Series &series, FeatureCache &cache) {
	if (!cache.second_diffs) {
		auto diffs = ComputeDiffs(series, cache);
		std::vector<double> second;
		if (diffs.size() >= 2) {
			second.reserve(diffs.size() - 1);
			for (size_t i = 1; i < diffs.size(); ++i) {
				second.push_back(diffs[i] - diffs[i - 1]);
			}
		}
		cache.second_diffs = std::move(second);
	}
	return *cache.second_diffs;
}

double ComputeAutocorrelation(const Series &series, size_t lag, FeatureCache &cache) {
	if (lag >= series.size() || series.size() < 2) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double mean = ComputeMean(series, cache);
	double variance = ComputeVariance(series, cache);
	if (variance < kEpsilon) {
		return 0.0;
	}
	double numerator = 0.0;
	for (size_t i = 0; i < series.size() - lag; ++i) {
		numerator += (series[i] - mean) * (series[i + lag] - mean);
	}
	return numerator / ((series.size() - lag) * variance);
}

const std::vector<double> &ComputeCumulativeSum(const Series &series, FeatureCache &cache) {
	if (!cache.cumulative_sum) {
		std::vector<double> cumsum(series.size());
		double running = 0.0;
		for (size_t i = 0; i < series.size(); ++i) {
			running += series[i];
			cumsum[i] = running;
		}
		cache.cumulative_sum = std::move(cumsum);
	}
	return *cache.cumulative_sum;
}

void ComputeFFT(const Series &series, FeatureCache &cache) {
	if (cache.fft_real && cache.fft_imag) {
		return;
	}
	size_t n = series.size();
	std::vector<double> real(n, 0.0), imag(n, 0.0);
	for (size_t k = 0; k < n; ++k) {
		double sum_real = 0.0;
		double sum_imag = 0.0;
		for (size_t t = 0; t < n; ++t) {
			double angle = -2.0 * kPi * static_cast<double>(k * t) / static_cast<double>(n);
			sum_real += series[t] * std::cos(angle);
			sum_imag += series[t] * std::sin(angle);
		}
		real[k] = sum_real;
		imag[k] = sum_imag;
	}
	cache.fft_real = std::move(real);
	cache.fft_imag = std::move(imag);
}

std::complex<double> GetFFTValue(const Series &series, size_t k, FeatureCache &cache) {
	if (series.empty()) {
		return {0.0, 0.0};
	}
	ComputeFFT(series, cache);
	if (!cache.fft_real || !cache.fft_imag || k >= cache.fft_real->size()) {
		return {0.0, 0.0};
	}
	return {(*cache.fft_real)[k], (*cache.fft_imag)[k]};
}

struct LinRegResult {
	double slope = std::numeric_limits<double>::quiet_NaN();
	double intercept = std::numeric_limits<double>::quiet_NaN();
	double rvalue = std::numeric_limits<double>::quiet_NaN();
	double pvalue = std::numeric_limits<double>::quiet_NaN();
	double std_error = std::numeric_limits<double>::quiet_NaN();
};

LinRegResult ComputeLinearRegression(const std::vector<double> &x, const Series &y) {
	LinRegResult result;
	if (x.size() != y.size() || x.size() < 2) {
		return result;
	}
	size_t n = x.size();
	double sum_x = std::accumulate(x.begin(), x.end(), 0.0);
	double sum_y = std::accumulate(y.begin(), y.end(), 0.0);
	double sum_xx = 0.0;
	double sum_xy = 0.0;
	for (size_t i = 0; i < n; ++i) {
		sum_xx += x[i] * x[i];
		sum_xy += x[i] * y[i];
	}
	double denominator = n * sum_xx - sum_x * sum_x;
	if (std::fabs(denominator) < kEpsilon) {
		return result;
	}
	result.slope = (n * sum_xy - sum_x * sum_y) / denominator;
	result.intercept = (sum_y - result.slope * sum_x) / static_cast<double>(n);

	double mean_y = sum_y / static_cast<double>(n);
	double ss_tot = 0.0;
	double ss_res = 0.0;
	for (size_t i = 0; i < n; ++i) {
		double yi = y[i];
		double yhat = result.intercept + result.slope * x[i];
		ss_tot += (yi - mean_y) * (yi - mean_y);
		ss_res += (yi - yhat) * (yi - yhat);
	}
	if (ss_tot > kEpsilon) {
		result.rvalue = std::sqrt(std::max(0.0, 1.0 - ss_res / ss_tot));
		if (result.slope < 0.0) {
			result.rvalue = -result.rvalue;
		}
	}
	if (n > 2) {
		double std_err = std::sqrt(ss_res / (n - 2)) / std::sqrt(sum_xx - sum_x * sum_x / n);
		result.std_error = std_err;
		if (std_err > kEpsilon) {
			double t_stat = result.slope / std_err;
			result.pvalue = NormalPValue(t_stat);
		}
	}
	return result;
}

double LinearTrend(const Series &series, FeatureCache &cache, std::string_view attr) {
	std::vector<double> x(series.size());
	for (size_t i = 0; i < x.size(); ++i) {
		x[i] = static_cast<double>(i);
	}
	auto lin = ComputeLinearRegression(x, series);
	if (attr == "slope") {
		return lin.slope;
	}
	if (attr == "intercept") {
		return lin.intercept;
	}
	if (attr == "rvalue") {
		return lin.rvalue;
	}
	if (attr == "stderr") {
		return lin.std_error;
	}
	if (attr == "pvalue") {
		return lin.pvalue;
	}
	return std::numeric_limits<double>::quiet_NaN();
}

double LinearTrendTimewise(const Series &series, FeatureCache &cache, std::string_view attr) {
	if (!cache.time_axis) {
		return LinearTrend(series, cache, attr);
	}
	auto lin = ComputeLinearRegression(*cache.time_axis, series);
	if (attr == "slope") {
		return lin.slope;
	}
	if (attr == "intercept") {
		return lin.intercept;
	}
	if (attr == "rvalue") {
		return lin.rvalue;
	}
	if (attr == "stderr") {
		return lin.std_error;
	}
	if (attr == "pvalue") {
		return lin.pvalue;
	}
	return std::numeric_limits<double>::quiet_NaN();
}

double AggLinearTrend(const Series &series, FeatureCache &, size_t chunk_len, std::string_view f_agg,
                      std::string_view attr) {
	if (chunk_len == 0 || series.size() < chunk_len) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	std::vector<double> attr_values;
	for (size_t i = 0; i + chunk_len <= series.size(); i += chunk_len) {
		std::vector<double> chunk(series.begin() + i, series.begin() + i + chunk_len);
		FeatureCache chunk_cache(chunk);
		attr_values.push_back(LinearTrend(chunk, chunk_cache, attr));
	}
	if (attr_values.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	if (f_agg == "max") {
		return *std::max_element(attr_values.begin(), attr_values.end());
	}
	if (f_agg == "min") {
		return *std::min_element(attr_values.begin(), attr_values.end());
	}
	if (f_agg == "mean") {
		return MeanOfVector(attr_values);
	}
	if (f_agg == "var") {
		double mean = MeanOfVector(attr_values);
		double accum = 0.0;
		for (auto value : attr_values) {
			double diff = value - mean;
			accum += diff * diff;
		}
		return accum / attr_values.size();
	}
	return std::numeric_limits<double>::quiet_NaN();
}

double SampleEntropy(const Series &series, int m, double r_fraction, FeatureCache &cache) {
	if (series.size() <= static_cast<size_t>(m + 1)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double stddev = ComputeStdDev(series, cache);
	if (stddev < kEpsilon) {
		return 0.0;
	}
	double r = r_fraction * stddev;
	size_t n = series.size();
	auto count_matches = [&](int mm) {
		size_t matches = 0;
		for (size_t i = 0; i < n - mm; ++i) {
			for (size_t j = i + 1; j < n - mm; ++j) {
				bool match = true;
				for (int k = 0; k < mm; ++k) {
					if (std::fabs(series[i + k] - series[j + k]) > r) {
						match = false;
						break;
					}
				}
				if (match) {
					++matches;
				}
			}
		}
		return matches;
	};
	double a = static_cast<double>(count_matches(m + 1));
	double b = static_cast<double>(count_matches(m));
	if (b == 0 || a == 0) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return -std::log(a / b);
}

double ApproximateEntropy(const Series &series, int m, double r, FeatureCache &cache) {
	if (series.size() <= static_cast<size_t>(m + 1)) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	double stddev = ComputeStdDev(series, cache);
	if (stddev < kEpsilon) {
		return 0.0;
	}
	double tolerance = r * stddev;
	auto phi = [&](int mm) {
		size_t n = series.size() - mm + 1;
		std::vector<double> C(n, 0.0);
		for (size_t i = 0; i < n; ++i) {
			size_t count = 0;
			for (size_t j = 0; j < n; ++j) {
				if (i == j) {
					continue;
				}
				bool match = true;
				for (int k = 0; k < mm; ++k) {
					if (std::fabs(series[i + k] - series[j + k]) > tolerance) {
						match = false;
						break;
					}
				}
				if (match) {
					++count;
				}
			}
			C[i] = static_cast<double>(count) / (n - 1);
		}
		double sum = 0.0;
		for (double value : C) {
			if (value > 0.0) {
				sum += std::log(value);
			}
		}
		return sum / n;
	};
	return phi(m) - phi(m + 1);
}

double PermutationEntropy(const Series &series, int dimension, int tau, FeatureCache &) {
	if (series.size() < static_cast<size_t>((dimension - 1) * tau + 1) || dimension <= 1) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	std::unordered_map<std::string, int> pattern_counts;
	for (size_t i = 0; i + (dimension - 1) * tau < series.size(); ++i) {
		std::vector<std::pair<double, int>> window;
		for (int j = 0; j < dimension; ++j) {
			window.emplace_back(series[i + j * tau], j);
		}
		std::sort(window.begin(), window.end(), [](const auto &a, const auto &b) {
			if (a.first == b.first) {
				return a.second < b.second;
			}
			return a.first < b.first;
		});
		std::string key;
		for (const auto &entry : window) {
			key.append(std::to_string(entry.second)).push_back(',');
		}
		pattern_counts[key]++;
	}
	double total = 0.0;
	for (const auto &kv : pattern_counts) {
		double p = static_cast<double>(kv.second);
		total += p;
	}
	double entropy = 0.0;
	for (const auto &kv : pattern_counts) {
		double p = kv.second / total;
		entropy -= p * std::log(p);
	}
	return entropy / std::log(static_cast<double>(dimension));
}

double LempelZivComplexity(const Series &series, int bins, FeatureCache &) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	auto minmax = std::minmax_element(series.begin(), series.end());
	double min_val = *minmax.first;
	double max_val = *minmax.second;
	if (std::fabs(max_val - min_val) < kEpsilon) {
		return 0.0;
	}
	std::vector<int> symbols(series.size());
	for (size_t i = 0; i < series.size(); ++i) {
		double normalized = (series[i] - min_val) / (max_val - min_val);
		int symbol = static_cast<int>(std::floor(normalized * bins));
		if (symbol >= bins) {
			symbol = bins - 1;
		}
		symbols[i] = symbol;
	}
	size_t n = symbols.size();
	size_t i = 0;
	size_t c = 1;
	size_t l = 1;
	size_t k = 1;
	while (true) {
		if (i + k > n) {
			c++;
			break;
		}
		bool match = true;
		for (size_t j = 0; j < k; ++j) {
			if (symbols[i + j] != symbols[l + j - 1]) {
				match = false;
				break;
			}
		}
		if (match) {
			k++;
			if (l + k - 1 > n) {
				c++;
				break;
			}
		} else {
			i++;
			if (i == l) {
				c++;
				l += k;
				if (l > n) {
					break;
				}
				i = 0;
				k = 1;
			} else {
				k = 1;
			}
		}
	}
	return static_cast<double>(c);
}

double FourierEntropy(const Series &series, int bins, FeatureCache &cache) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	ComputeFFT(series, cache);
	const auto &real = *cache.fft_real;
	const auto &imag = *cache.fft_imag;
	std::vector<double> magnitudes(real.size());
	for (size_t i = 0; i < real.size(); ++i) {
		magnitudes[i] = std::sqrt(real[i] * real[i] + imag[i] * imag[i]);
	}
	auto minmax = std::minmax_element(magnitudes.begin(), magnitudes.end());
	double min_val = *minmax.first;
	double max_val = *minmax.second;
	if (std::fabs(max_val - min_val) < kEpsilon) {
		return 0.0;
	}
	std::vector<int> histogram(bins, 0);
	for (double mag : magnitudes) {
		double normalized = (mag - min_val) / (max_val - min_val);
		int idx = static_cast<int>(std::floor(normalized * bins));
		if (idx >= bins) {
			idx = bins - 1;
		}
		++histogram[idx];
	}
	double total = static_cast<double>(magnitudes.size());
	double entropy = 0.0;
	for (int count : histogram) {
		if (count == 0) {
			continue;
		}
		double p = count / total;
		entropy -= p * std::log(p);
	}
	return entropy;
}

double BenfordCorrelation(const Series &series) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	std::vector<double> expected(9);
	for (int d = 1; d <= 9; ++d) {
		expected[d - 1] = std::log10(1.0 + 1.0 / d);
	}
	std::vector<double> observed(9, 0.0);
	double total = 0.0;
	for (double value : series) {
		double abs_value = std::fabs(value);
		if (abs_value < kEpsilon) {
			continue;
		}
		while (abs_value >= 10.0) {
			abs_value /= 10.0;
		}
		while (abs_value < 1.0) {
			abs_value *= 10.0;
		}
		int digit = static_cast<int>(abs_value);
		if (digit >= 1 && digit <= 9) {
			observed[digit - 1] += 1.0;
			total += 1.0;
		}
	}
	if (total == 0.0) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	for (double &value : observed) {
		value /= total;
	}
	double mean_expected = MeanOfVector(expected);
	double mean_observed = MeanOfVector(observed);
	double numerator = 0.0;
	double denom_exp = 0.0;
	double denom_obs = 0.0;
	for (size_t i = 0; i < expected.size(); ++i) {
		double de = expected[i] - mean_expected;
		double dobs = observed[i] - mean_observed;
		numerator += de * dobs;
		denom_exp += de * de;
		denom_obs += dobs * dobs;
	}
	if (denom_exp < kEpsilon || denom_obs < kEpsilon) {
		return 0.0;
	}
	return numerator / std::sqrt(denom_exp * denom_obs);
}

double MatrixProfileValue(const Series &series, double threshold, std::string_view feature, FeatureCache &) {
	if (series.size() < 4) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	// Simple O(n^2) matrix profile approximation using Euclidean distance
	size_t window = std::max<size_t>(4, series.size() / 10);
	std::vector<double> profile(series.size(), std::numeric_limits<double>::infinity());
	for (size_t i = 0; i + window <= series.size(); ++i) {
		for (size_t j = i + window; j + window <= series.size(); ++j) {
			double dist = 0.0;
			for (size_t k = 0; k < window; ++k) {
				double diff = series[i + k] - series[j + k];
				dist += diff * diff;
			}
			dist = std::sqrt(dist);
			profile[i] = std::min(profile[i], dist);
			profile[j] = std::min(profile[j], dist);
		}
	}
	std::vector<double> filtered;
	for (double value : profile) {
		if (std::isfinite(value) && value >= threshold) {
			filtered.push_back(value);
		}
	}
	if (filtered.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	if (feature == "min") {
		return *std::min_element(filtered.begin(), filtered.end());
	}
	if (feature == "max") {
		return *std::max_element(filtered.begin(), filtered.end());
	}
	if (feature == "mean") {
		return MeanOfVector(filtered);
	}
	if (feature == "median") {
		std::sort(filtered.begin(), filtered.end());
		size_t n = filtered.size();
		if (n % 2 == 1) {
			return filtered[n / 2];
		}
		return (filtered[n / 2 - 1] + filtered[n / 2]) / 2.0;
	}
	if (feature == "25") {
		std::sort(filtered.begin(), filtered.end());
		return filtered[static_cast<size_t>(0.25 * filtered.size())];
	}
	if (feature == "75") {
		std::sort(filtered.begin(), filtered.end());
		return filtered[static_cast<size_t>(0.75 * filtered.size())];
	}
	return MeanOfVector(filtered);
}

double QuerySimilarityCount(const Series &series, const std::vector<double> &query, double threshold,
                            FeatureCache &) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	std::vector<double> pattern = query;
	if (pattern.empty()) {
		pattern = series;
	}
	size_t window = std::min<size_t>(pattern.size(), series.size());
	if (window == 0) {
		return 0.0;
	}
	auto z_normalize = [](const std::vector<double> &values) {
		double mean = MeanOfVector(values);
		double accum = 0.0;
		for (double v : values) {
			accum += (v - mean) * (v - mean);
		}
		double stddev = std::sqrt(accum / values.size());
		std::vector<double> normalized(values.size());
		if (stddev < kEpsilon) {
			std::fill(normalized.begin(), normalized.end(), 0.0);
		} else {
			for (size_t i = 0; i < values.size(); ++i) {
				normalized[i] = (values[i] - mean) / stddev;
			}
		}
		return normalized;
	};
	auto normalized_pattern = z_normalize(pattern);
	size_t matches = 0;
	for (size_t i = 0; i + window <= series.size(); ++i) {
		std::vector<double> window_values(series.begin() + i, series.begin() + i + window);
		auto normalized_window = z_normalize(window_values);
		double dist = 0.0;
		for (size_t k = 0; k < window; ++k) {
			double diff = normalized_pattern[k] - normalized_window[k];
			dist += diff * diff;
		}
		dist = std::sqrt(dist);
		if (dist <= threshold) {
			++matches;
		}
	}
	return static_cast<double>(matches);
}

double RickerWavelet(double t, double width) {
	double scaled = t / width;
	double factor = (2.0 / (std::sqrt(3.0 * width) * std::pow(kPi, 0.25)));
	return factor * (1.0 - scaled * scaled) * std::exp(-scaled * scaled / 2.0);
}

double CwtCoefficient(const Series &series, FeatureCache &, const std::vector<int64_t> &widths, size_t coeff_index,
                      int64_t w) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	if (coeff_index >= widths.size()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	int64_t width = widths[coeff_index];
	std::vector<double> wavelet(series.size());
	double center = static_cast<double>(series.size()) / 2.0;
	for (size_t i = 0; i < series.size(); ++i) {
		wavelet[i] = RickerWavelet(static_cast<double>(i) - center, width);
	}
	double coeff = 0.0;
	for (size_t i = 0; i < series.size(); ++i) {
		coeff += series[i] * wavelet[i];
	}
	return coeff;
}

double SpktWelchDensity(const Series &series, FeatureCache &, size_t coeff_index) {
	if (series.size() < 8) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	size_t segment_length = std::min<size_t>(256, series.size());
	size_t step = segment_length / 2;
	std::vector<double> window(segment_length);
	for (size_t i = 0; i < segment_length; ++i) {
		window[i] = 0.54 - 0.46 * std::cos(2.0 * kPi * i / (segment_length - 1));
	}
	std::vector<double> psd(segment_length / 2 + 1, 0.0);
	size_t segments = 0;
	for (size_t start = 0; start + segment_length <= series.size(); start += step) {
		++segments;
		for (size_t k = 0; k < psd.size(); ++k) {
			std::complex<double> accum(0.0, 0.0);
			for (size_t n = 0; n < segment_length; ++n) {
				double angle = -2.0 * kPi * k * n / segment_length;
				accum += series[start + n] * window[n] * std::complex<double>(std::cos(angle), std::sin(angle));
			}
			psd[k] += std::norm(accum) / segment_length;
		}
	}
	if (segments == 0) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	for (double &value : psd) {
		value /= segments;
	}
	if (coeff_index >= psd.size()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return psd[coeff_index];
}

double NumberCwtPeaks(const Series &series, int n) {
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

double MatrixProfileThresholdCount(const Series &series, size_t window, double threshold) {
	if (window == 0 || series.size() < window) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	size_t count = 0;
	for (size_t i = 0; i + window <= series.size(); ++i) {
		double energy = 0.0;
		for (size_t j = 0; j < window; ++j) {
			energy += std::fabs(series[i + j]);
		}
		if (energy >= threshold) {
			++count;
		}
	}
	return static_cast<double>(count);
}

} // namespace anofoxtime::features

