#include "anofox-time/features/feature_math.hpp"
#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <numeric>
#include <queue>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <set>

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
	
	// scipy.stats.skew uses sample variance (ddof=1), not population variance
	// Calculate sample variance: sum((x - mean)^2) / (n - 1)
	double sum_sq_diff = 0.0;
	double sum_cub_diff = 0.0;
	for (double value : series) {
		double diff = value - mean;
		sum_sq_diff += diff * diff;
		sum_cub_diff += diff * diff * diff;
	}
	double n = static_cast<double>(series.size());
	if (n < 3.0 || sum_sq_diff < kEpsilon) {
		return 0.0;
	}
	
	// pandas.Series.skew() formula (what tsfresh uses): (n / ((n-1)*(n-2))) * sum(((x-mean)/std)^3)
	// where std is sample std (ddof=1) = sqrt(sum_sq_diff / (n-1))
	double sample_variance = sum_sq_diff / (n - 1.0);
	double sample_std = std::sqrt(sample_variance);
	if (sample_std < kEpsilon) {
		return 0.0;
	}
	// Calculate sum(((x-mean)/std)^3) = sum((x-mean)^3) / std^3
	double normalized_sum = sum_cub_diff / (sample_std * sample_std * sample_std);
	return (n / ((n - 1.0) * (n - 2.0))) * normalized_sum;
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
	double accumulator = 0.0;
	for (double value : series) {
		double diff = value - mean;
		accumulator += diff * diff * diff * diff;
	}
	double n = static_cast<double>(series.size());
	// Use sample variance for kurtosis to match pandas/tsfresh
	// sample_variance = variance * n / (n-1)
	double sample_variance = variance * n / (n - 1.0);
	double sample_variance2 = sample_variance * sample_variance;
	// Fisher-Pearson excess kurtosis formula with sample variance:
	// g2 = (n*(n+1)/((n-1)*(n-2)*(n-3))) * sum((x-mu)^4/s^4) - 3*(n-1)^2/((n-2)*(n-3))
	// where s^2 is sample variance
	double kurtosis = (n * (n + 1.0) * accumulator) / (sample_variance2 * (n - 1.0) * (n - 2.0) * (n - 3.0)) -
	                  (3.0 * std::pow(n - 1.0, 2)) / ((n - 2.0) * (n - 3.0));
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
	// tsfresh agg_linear_trend: 
	// 1. Aggregate chunks using f_agg (max, min, mean, median)
	// 2. Do linear regression on aggregated values vs indices (0, 1, 2, ...)
	// 3. Return the requested attribute from the regression
	std::vector<double> aggregated_values;
	for (size_t i = 0; i < series.size(); i += chunk_len) {
		size_t end = std::min(i + chunk_len, series.size());
		std::vector<double> chunk(series.begin() + i, series.begin() + end);
		if (chunk.empty()) {
			continue;
		}
		double agg_value = 0.0;
		if (f_agg == "max") {
			agg_value = *std::max_element(chunk.begin(), chunk.end());
		} else if (f_agg == "min") {
			agg_value = *std::min_element(chunk.begin(), chunk.end());
		} else if (f_agg == "mean") {
			agg_value = MeanOfVector(chunk);
		} else if (f_agg == "median") {
			std::sort(chunk.begin(), chunk.end());
			size_t mid = chunk.size() / 2;
			if (chunk.size() % 2 == 0) {
				agg_value = (chunk[mid - 1] + chunk[mid]) / 2.0;
			} else {
				agg_value = chunk[mid];
			}
		} else {
			return std::numeric_limits<double>::quiet_NaN();
		}
		aggregated_values.push_back(agg_value);
	}
	if (aggregated_values.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	// Linear regression on aggregated values vs indices (0, 1, 2, ...)
	std::vector<double> indices(aggregated_values.size());
	for (size_t i = 0; i < indices.size(); ++i) {
		indices[i] = static_cast<double>(i);
	}
	auto lin = ComputeLinearRegression(indices, aggregated_values);
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
	
	// tsfresh: Uses _into_subchunks to create templates, then counts matches including self and subtracts 1
	// For each template, count all matches (including self), then subtract 1 (exclude self)
	auto count_matches = [&](int mm) {
		size_t total_matches = 0;
		// For each template of length mm
		for (size_t i = 0; i <= n - mm; ++i) {
		size_t matches = 0;
			// Check against all templates (including self)
			for (size_t j = 0; j <= n - mm; ++j) {
				// Compute max absolute difference
				double max_diff = 0.0;
				for (int k = 0; k < mm; ++k) {
					double diff = std::fabs(series[i + k] - series[j + k]);
					if (diff > max_diff) {
						max_diff = diff;
					}
				}
				if (max_diff <= r) {
					++matches;
				}
			}
			// Subtract 1 to exclude self-match (as tsfresh does)
			total_matches += matches - 1;
		}
		return total_matches;
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
	size_t N = series.size();
	auto phi = [&](int mm) {
		size_t n = N - mm + 1;
		std::vector<double> C(n, 0.0);
		for (size_t i = 0; i < n; ++i) {
			size_t count = 0;
			for (size_t j = 0; j < n; ++j) {
				// tsfresh includes self-comparison (i==j case)
				double max_diff = 0.0;
				for (int k = 0; k < mm; ++k) {
					double diff = std::fabs(series[i + k] - series[j + k]);
					if (diff > max_diff) {
						max_diff = diff;
					}
				}
				if (max_diff <= tolerance) {
					++count;
				}
			}
			// tsfresh: C = count / (N - m + 1)
			C[i] = static_cast<double>(count) / static_cast<double>(n);
		}
		double sum = 0.0;
		for (double value : C) {
			if (value > 0.0) {
				sum += std::log(value);
			}
		}
		return sum / static_cast<double>(n);
	};
	// tsfresh: approximate_entropy = abs(phi(m) - phi(m+1))
	// Returns absolute value of the difference
	return std::fabs(phi(m) - phi(m + 1));
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
	// tsfresh returns raw entropy without normalization (no division by log(dimension))
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
	return entropy;
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
	// tsfresh bins using np.linspace and np.searchsorted
	// Create bin edges: np.linspace(min, max, bins + 1)[1:]
	// This creates bins+1 points, then takes [1:] to get bins edges
	// Actually, np.linspace(min, max, bins+1) creates bins+1 points from min to max (inclusive)
	// Then [1:] takes all but the first, giving us bins edges
	// But wait - if we want bins bins, we need bins+1 points total
	// np.linspace(min, max, bins+1) = [min, ..., max] with bins+1 points
	// [1:] = [point1, point2, ..., point_bins] = bins edges
	// So the first bin is [min, point1), second is [point1, point2), etc.
	std::vector<double> bin_edges(bins);
	double step = (max_val - min_val) / static_cast<double>(bins);
	for (int i = 0; i < bins; ++i) {
		bin_edges[i] = min_val + step * (i + 1);
	}
	
	// Assign each value to a bin using searchsorted (left side)
	// np.searchsorted(bins, x, side="left") finds the insertion point
	// Use binary search for efficiency
	std::vector<int> symbols(series.size());
	for (size_t i = 0; i < series.size(); ++i) {
		// Binary search for the bin
		// We want the leftmost position where bin_edges[pos] >= series[i]
		int left = 0;
		int right = bins;
		int bin_idx = bins;  // Default to last bin if value >= all edges
		while (left < right) {
			int mid = (left + right) / 2;
			if (series[i] < bin_edges[mid]) {
				bin_idx = mid;
				right = mid;
			} else {
				left = mid + 1;
			}
		}
		// If value >= all edges, it goes in the last bin
		if (bin_idx >= bins) {
			bin_idx = bins - 1;
		}
		symbols[i] = bin_idx;
	}
	
	// tsfresh's Lempel-Ziv complexity algorithm:
	// sub_strings = set()
	// n = len(sequence)
	// ind = 0
	// inc = 1
	// while ind + inc <= n:
	//     sub_str = tuple(sequence[ind : ind + inc])
	//     if sub_str in sub_strings:
	//         inc += 1
	//     else:
	//         sub_strings.add(sub_str)
	//         ind += inc
	//         inc = 1
	// return len(sub_strings) / n
	
	size_t n = symbols.size();
	if (n == 0) {
		return 0.0;
	}
	
	// Use a set to track unique substrings
	// std::set uses operator< for comparison, which std::vector provides
	// This is simpler and more reliable than unordered_set with custom hash
	std::set<std::vector<int>> sub_strings;
	size_t ind = 0;
	size_t inc = 1;
	
	while (ind + inc <= n) {
		// Create substring: symbols[ind : ind + inc]
		std::vector<int> sub_str(symbols.begin() + ind, symbols.begin() + ind + inc);
		
		// Check if this substring is already in sub_strings
		auto it = sub_strings.find(sub_str);
		if (it != sub_strings.end()) {
			// Substring exists, extend it
			inc++;
		} else {
			// New substring, add it and move forward
			sub_strings.insert(sub_str);
			ind += inc;
			inc = 1;
		}
	}
	
	// tsfresh returns len(sub_strings) / n
	double result = static_cast<double>(sub_strings.size()) / static_cast<double>(n);
	
	return result;
}

double FourierEntropy(const Series &series, int bins, FeatureCache &cache) {
	if (series.empty()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	// tsfresh uses welch method (power spectral density) with Hann window, then normalizes by max
	// Use same welch method as SpktWelchDensity but get full PSD
	size_t max_length_per_segment = 256;
	size_t segment_length = std::min<size_t>(max_length_per_segment, series.size());
	size_t step = std::max<size_t>(1, segment_length / 2);
	
	// Use Hann window (same as SpktWelchDensity): w(n) = 0.5 * (1 - cos(2πn/N))
	std::vector<double> window(segment_length);
	double window_power = 0.0;
	for (size_t i = 0; i < segment_length; ++i) {
		if (segment_length == 1) {
			window[i] = 1.0;
		} else {
			window[i] = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(segment_length)));
		}
		window_power += window[i] * window[i];
	}
	
	std::vector<double> pxx(segment_length / 2 + 1, 0.0);
	size_t segments = 0;
	for (size_t start = 0; start + segment_length <= series.size(); start += step) {
		++segments;
		
		// scipy welch default: detrend='constant' (remove mean from each segment)
		double segment_mean = 0.0;
		for (size_t n = 0; n < segment_length; ++n) {
			segment_mean += series[start + n];
		}
		segment_mean /= static_cast<double>(segment_length);
		
		for (size_t k = 0; k < pxx.size(); ++k) {
			std::complex<double> accum(0.0, 0.0);
			for (size_t n = 0; n < segment_length; ++n) {
				// Detrend: subtract mean before windowing and FFT
				double detrended = series[start + n] - segment_mean;
				double angle = -2.0 * kPi * k * n / segment_length;
				accum += detrended * window[n] * std::complex<double>(std::cos(angle), std::sin(angle));
			}
			// Use same PSD calculation as SpktWelchDensity (proper scipy welch formula)
			double scaling = (k == 0 || k == pxx.size() - 1) ? 1.0 : 2.0;
			if (window_power > 0.0) {
				// scipy welch: PSD = |FFT|^2 * scaling / sum(window^2)
				pxx[k] += std::norm(accum) * scaling / window_power;
			}
		}
	}
	if (segments == 0) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	// Average over segments
	for (double &value : pxx) {
		value /= segments;
	}
	
	// Normalize by max as tsfresh does
	auto max_it = std::max_element(pxx.begin(), pxx.end());
	double max_psd = *max_it;
	if (max_psd < kEpsilon) {
		return 0.0;
	}
	for (double &value : pxx) {
		value /= max_psd;
	}
	
	// Now compute binned entropy (like binned_entropy function)
	// tsfresh uses np.histogram with range=(0, 1), which creates bins [0, 0.5), [0.5, 1.0]
	// For value=1.0, it goes into the last bin
	std::vector<int> histogram(bins, 0);
	for (double value : pxx) {
		// Values are already normalized 0-1, bin them
		// Use same logic as numpy histogram: floor(value * bins) but handle value=1.0 specially
		int idx;
		if (value >= 1.0 - 1e-12) {
			idx = bins - 1;  // Value at or very close to 1.0 goes to last bin
		} else {
			idx = static_cast<int>(std::floor(value * bins));
			if (idx >= bins) {
				idx = bins - 1;
			}
		}
		if (idx < 0) {
			idx = 0;
		}
		++histogram[idx];
	}
	
	// tsfresh binned_entropy: sets probs[probs == 0] = 1.0 before log
	double total = static_cast<double>(pxx.size());
	double entropy = 0.0;
	for (int count : histogram) {
		double p = count / total;
		// tsfresh sets zero probs to 1.0 before log, which makes log(1.0) = 0 (no contribution)
		// So we skip zero counts (equivalent)
		if (p > 0.0) {
		entropy -= p * std::log(p);
		}
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
	size_t step = std::max<size_t>(1, segment_length / 2);  // Ensure step >= 1 to avoid infinite loop
	
	// tsfresh uses scipy.signal.welch with default window='hann' (Hann window)
	// scipy's hann window: w(n) = 0.5 * (1 - cos(2πn/N)) for n=0..N-1
	// Note: scipy uses n/N, not n/(N-1) like numpy hanning
	std::vector<double> window(segment_length);
	double window_power = 0.0;  // Sum of squared window values for normalization
	for (size_t i = 0; i < segment_length; ++i) {
		if (segment_length == 1) {
			window[i] = 1.0;
		} else {
			window[i] = 0.5 * (1.0 - std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(segment_length)));
		}
		window_power += window[i] * window[i];
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
			// scipy welch formula for density scaling with fs=1:
			// According to scipy source code: scale = 1.0 / (fs * (win*win).sum())
			// With fs=1: scale = 1.0 / sum(window^2)
			// PSD = |FFT|^2 * scale = |FFT|^2 / sum(window^2)
			// For one-sided PSD (real signals), multiply non-DC/Nyquist by 2:
			// PSD = |FFT|^2 * 2 / sum(window^2) for non-DC/Nyquist
			// PSD = |FFT|^2 / sum(window^2) for DC (k=0) and Nyquist (k=psd.size()-1)
			// Note: scipy welch averages over segments, so we accumulate and divide by segments later
			double scaling = (k == 0 || k == psd.size() - 1) ? 1.0 : 2.0;
			if (window_power > 0.0) {
				psd[k] += std::norm(accum) * scaling / window_power;
			}
		}
	}
	if (segments == 0) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	// Average over segments
	for (double &value : psd) {
		value /= segments;
	}
	if (coeff_index >= psd.size()) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	return psd[coeff_index];
}

namespace {
	// Helper: Find relative maxima in a CWT row (scale)
	std::vector<size_t> FindRelativeMaxima(const std::vector<double> &cwt_row) {
		std::vector<size_t> maxima;
		if (cwt_row.size() < 2) {
			return maxima;
		}
		// Check interior points
		for (size_t i = 1; i + 1 < cwt_row.size(); ++i) {
			if (cwt_row[i] > cwt_row[i - 1] && cwt_row[i] > cwt_row[i + 1]) {
				maxima.push_back(i);
			}
		}
		// Check boundaries
		if (cwt_row[0] > cwt_row[1]) {
			maxima.push_back(0);
		}
		if (cwt_row[cwt_row.size() - 1] > cwt_row[cwt_row.size() - 2]) {
			maxima.push_back(cwt_row.size() - 1);
		}
		return maxima;
	}
	
	// Helper: Calculate percentile of values (for noise calculation)
	double Percentile(const std::vector<double> &values, double percentile) {
		if (values.empty()) {
			return 0.0;
		}
		std::vector<double> sorted = values;
		std::sort(sorted.begin(), sorted.end());
		double index = percentile / 100.0 * (sorted.size() - 1);
		size_t lower = static_cast<size_t>(std::floor(index));
		size_t upper = static_cast<size_t>(std::ceil(index));
		if (lower == upper) {
			return sorted[lower];
		}
		double weight = index - lower;
		return sorted[lower] * (1.0 - weight) + sorted[upper] * weight;
	}
}

double NumberCwtPeaks(const Series &series, int n) {
	// tsfresh number_cwt_peaks uses scipy.signal.find_peaks_cwt
	// which applies CWT with Ricker wavelets at widths [1, 2, ..., n]
	// then finds peaks using ridge line detection with SNR filtering
	if (series.size() < static_cast<size_t>(2 * n + 1) || n <= 0) {
		return 0.0;
	}
	
	// Build CWT matrix: rows are positions, columns are widths
	std::vector<std::vector<double>> cwt_matrix(series.size());
	for (size_t i = 0; i < series.size(); ++i) {
		cwt_matrix[i].resize(static_cast<size_t>(n), 0.0);
	}
	
	// Compute CWT at each width
	for (int width = 1; width <= n; ++width) {
		size_t width_idx = static_cast<size_t>(width - 1);
		for (size_t pos = 0; pos < series.size(); ++pos) {
			double cwt_val = 0.0;
			// Convolve series with Ricker wavelet centered at pos
			int support = std::min(static_cast<int>(5 * width), static_cast<int>(series.size()));
			for (int offset = -support; offset <= support; ++offset) {
				int idx = static_cast<int>(pos) + offset;
				if (idx >= 0 && static_cast<size_t>(idx) < series.size()) {
					double t = static_cast<double>(offset);
					double wavelet_val = RickerWavelet(t, static_cast<double>(width));
					cwt_val += series[static_cast<size_t>(idx)] * wavelet_val;
				}
			}
			cwt_matrix[pos][width_idx] = cwt_val;
		}
	}
	
	// scipy find_peaks_cwt default parameters
	double min_snr = 1.0;
	double noise_perc = 10.0;
	size_t window_size = std::max<size_t>(1, series.size() / 20);
	
	// For n=1, simpler case: find local maxima and filter by SNR
	// scipy still uses ridge line algorithm but with single scale
	if (n == 1) {
		// Extract CWT row for width=1
		std::vector<double> cwt_row(series.size());
		for (size_t i = 0; i < series.size(); ++i) {
			cwt_row[i] = cwt_matrix[i][0];
		}
		std::vector<size_t> maxima = FindRelativeMaxima(cwt_row);
		if (maxima.empty()) {
			return 0.0;
		}
		
		// scipy find_peaks_cwt algorithm for n=1:
		// 1. Find relative maxima (local maxima in CWT)
		// 2. For the largest ridge line (max CWT value), calculate noise from window around it
		// 3. Signal is max CWT value, noise is noise_perc percentile of values in window
		// 4. Each peak's SNR = peak_value / noise_floor, filter by min_snr
		
		// Find maximum CWT value (largest ridge line reference)
		double max_signal = 0.0;
		size_t max_pos = 0;
		for (size_t i = 0; i < series.size(); ++i) {
			double abs_val = std::fabs(cwt_matrix[i][0]);
			if (abs_val > max_signal) {
				max_signal = abs_val;
				max_pos = i;
			}
		}
		
		// Calculate noise from window_size around maximum position
		// scipy uses window_size = cwt.shape[1] / 20, which for n=1 is series.size() / 20
		std::vector<double> noise_values;
		size_t start = (max_pos >= window_size) ? (max_pos - window_size) : 0;
		size_t end = std::min(max_pos + window_size + 1, series.size());
		for (size_t i = start; i < end; ++i) {
			noise_values.push_back(std::fabs(cwt_matrix[i][0]));
		}
		// Ensure we have enough values for percentile calculation
		if (noise_values.size() < 10) {
			// Use all CWT values if window is too small
			noise_values.clear();
			for (size_t i = 0; i < series.size(); ++i) {
				noise_values.push_back(std::fabs(cwt_matrix[i][0]));
			}
		}
		double noise_floor = Percentile(noise_values, noise_perc);
		
		// Filter peaks by SNR: each peak's value / noise_floor >= min_snr
		size_t peak_count = 0;
		for (size_t pos : maxima) {
			double peak_value = std::fabs(cwt_matrix[pos][0]);
			double snr = (noise_floor > 1e-12) ? (peak_value / noise_floor) : 0.0;
			if (snr >= min_snr) {
				++peak_count;
			}
		}
		return static_cast<double>(peak_count);
	}
	
	// For n>1, implement ridge line detection
	// Find relative maxima at each scale
	std::vector<std::vector<size_t>> maxima_per_scale(static_cast<size_t>(n));
	for (int width = 1; width <= n; ++width) {
		size_t width_idx = static_cast<size_t>(width - 1);
		std::vector<double> cwt_row(series.size());
		for (size_t i = 0; i < series.size(); ++i) {
			cwt_row[i] = cwt_matrix[i][width_idx];
		}
		maxima_per_scale[width_idx] = FindRelativeMaxima(cwt_row);
	}
	
	// Default parameters for ridge line detection
	std::vector<double> max_distances(static_cast<size_t>(n));
	for (int i = 0; i < n; ++i) {
		max_distances[i] = static_cast<double>(i + 1) / 4.0;  // widths / 4
	}
	double gap_thresh = static_cast<double>(n) / 4.0;  // min_length default
	size_t min_length = std::max<size_t>(1, static_cast<size_t>(n) / 4);
	
	// Track ridge lines: each ridge line is a list of (scale_idx, position) pairs
	struct RidgePoint {
		size_t scale_idx;
		size_t position;
		double value;
	};
	std::vector<std::vector<RidgePoint>> ridge_lines;
	
	// Start ridge lines from first scale
	for (size_t pos : maxima_per_scale[0]) {
		std::vector<RidgePoint> ridge;
		ridge.push_back({0, pos, cwt_matrix[pos][0]});
		ridge_lines.push_back(ridge);
	}
	
	// Connect maxima across scales
	for (size_t scale = 1; scale < static_cast<size_t>(n); ++scale) {
		std::vector<std::vector<RidgePoint>> new_ridge_lines;
		
		for (auto &ridge : ridge_lines) {
			RidgePoint last_point = ridge.back();
			double max_dist = max_distances[scale];
			
			// Find nearest maximum in current scale within max_dist
			size_t best_pos = SIZE_MAX;
			double best_dist = max_dist + 1.0;
			
			for (size_t pos : maxima_per_scale[scale]) {
				double dist = std::fabs(static_cast<double>(pos) - static_cast<double>(last_point.position));
				if (dist <= max_dist && dist < best_dist) {
					best_dist = dist;
					best_pos = pos;
				}
			}
			
			if (best_pos != SIZE_MAX) {
				// Extend ridge line
				ridge.push_back({scale, best_pos, cwt_matrix[best_pos][scale]});
				new_ridge_lines.push_back(ridge);
			} else {
				// Gap: allow if within gap_thresh
				if (ridge.size() < static_cast<size_t>(gap_thresh)) {
					// Can't extend, but keep if long enough
					if (ridge.size() >= min_length) {
						new_ridge_lines.push_back(ridge);
					}
				} else {
					// Too many gaps, discard
				}
			}
		}
		
		ridge_lines = new_ridge_lines;
	}
	
	// Filter by minimum length
	std::vector<std::vector<RidgePoint>> valid_ridges;
	for (const auto &ridge : ridge_lines) {
		if (ridge.size() >= min_length) {
			valid_ridges.push_back(ridge);
		}
	}
	
	// Apply SNR filtering
	std::vector<size_t> final_peaks;
	for (const auto &ridge : valid_ridges) {
		// Find maximum CWT value in ridge line (signal)
		double signal = 0.0;
		size_t peak_pos = 0;
		for (const auto &point : ridge) {
			double abs_val = std::fabs(point.value);
			if (abs_val > signal) {
				signal = abs_val;
				peak_pos = point.position;
			}
		}
		
		// Calculate noise from CWT values in ridge line
		std::vector<double> ridge_values;
		for (const auto &point : ridge) {
			ridge_values.push_back(std::fabs(point.value));
		}
		double noise = Percentile(ridge_values, noise_perc);
		
		// Check SNR
		double snr = (noise > 1e-12) ? (signal / noise) : 0.0;
		if (snr >= min_snr) {
			final_peaks.push_back(peak_pos);
		}
	}
	
	// Remove duplicates and count
	std::sort(final_peaks.begin(), final_peaks.end());
	final_peaks.erase(std::unique(final_peaks.begin(), final_peaks.end()), final_peaks.end());
	
	return static_cast<double>(final_peaks.size());
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

