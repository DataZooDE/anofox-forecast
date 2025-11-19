#pragma once

#include "anofox-time/features/feature_types.hpp"
#include <complex>
#include <optional>

namespace anofoxtime::features {

struct FeatureCache {
	const Series *series = nullptr;
	const std::vector<double> *time_axis = nullptr;
	mutable std::optional<double> mean;
	mutable std::optional<double> variance;
	mutable std::optional<double> stddev;
	mutable std::optional<double> median;
	mutable std::optional<std::vector<double>> sorted_values;
	mutable std::optional<std::vector<double>> abs_sorted_values;
	mutable std::optional<std::vector<double>> diffs;
	mutable std::optional<std::vector<double>> second_diffs;
	mutable std::optional<std::vector<double>> cumulative_sum;
	mutable std::optional<std::vector<double>> fft_real;
	mutable std::optional<std::vector<double>> fft_imag;

	explicit FeatureCache(const Series &series_ref, const std::vector<double> *time_axis_values = nullptr)
	    : series(&series_ref), time_axis(time_axis_values) {
	}
};

double ComputeMean(const Series &series, FeatureCache &cache);
double ComputeVariance(const Series &series, FeatureCache &cache);
double ComputeStdDev(const Series &series, FeatureCache &cache);
double ComputeMedian(const Series &series, FeatureCache &cache);
double ComputeSkewness(const Series &series, FeatureCache &cache);
double ComputeKurtosis(const Series &series, FeatureCache &cache);
double ComputeQuantile(const Series &series, double q, FeatureCache &cache);
double ComputeAutocorrelation(const Series &series, size_t lag, FeatureCache &cache);
std::vector<double> ComputeDiffs(const Series &series, FeatureCache &cache);
std::vector<double> ComputeSecondDiffs(const Series &series, FeatureCache &cache);
std::vector<double> ComputeSorted(const Series &series, FeatureCache &cache);
std::vector<double> ComputeAbsSorted(const Series &series, FeatureCache &cache);
const std::vector<double> &ComputeCumulativeSum(const Series &series, FeatureCache &cache);
void ComputeFFT(const Series &series, FeatureCache &cache);
std::complex<double> GetFFTValue(const Series &series, size_t k, FeatureCache &cache);

double LinearTrend(const Series &series, FeatureCache &cache, std::string_view attr);
double LinearTrendTimewise(const Series &series, FeatureCache &cache, std::string_view attr);
double AggLinearTrend(const Series &series, FeatureCache &cache, size_t chunk_len, std::string_view f_agg,
                      std::string_view attr);

double SampleEntropy(const Series &series, int m, double r_fraction, FeatureCache &cache);
double ApproximateEntropy(const Series &series, int m, double r, FeatureCache &cache);
double PermutationEntropy(const Series &series, int dimension, int tau, FeatureCache &cache);
double LempelZivComplexity(const Series &series, int bins, FeatureCache &cache);
double FourierEntropy(const Series &series, int bins, FeatureCache &cache);
double BenfordCorrelation(const Series &series);

double MatrixProfileValue(const Series &series, double threshold, std::string_view feature, FeatureCache &cache);
double QuerySimilarityCount(const Series &series, const std::vector<double> &query, double threshold,
                            FeatureCache &cache);

double CwtCoefficient(const Series &series, FeatureCache &cache, const std::vector<int64_t> &widths, size_t coeff_index,
                      int64_t w);
double SpktWelchDensity(const Series &series, FeatureCache &cache, size_t coeff_index);
double NumberCwtPeaks(const Series &series, int n);
double MatrixProfileThresholdCount(const Series &series, size_t window, double threshold);

} // namespace anofoxtime::features

