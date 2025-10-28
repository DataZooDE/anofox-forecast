#include "anofox-time/detectors/mad.hpp"
#include <vector>
#include <algorithm>
#include <cmath>

namespace anofoxtime::detectors {

namespace {
// Helper function to calculate the median of a vector of doubles.
// Note: This function modifies the input vector by sorting it.
double calculate_median(std::vector<double> &data) {
	if (data.empty()) {
		return 0.0;
	}
	size_t n = data.size();
	size_t mid = n / 2;
	std::nth_element(data.begin(), data.begin() + mid, data.end());
	if (n % 2 == 0) {
		double mid_val1 = data[mid];
		std::nth_element(data.begin(), data.begin() + mid - 1, data.end());
		double mid_val2 = data[mid - 1];
		return (mid_val1 + mid_val2) / 2.0;
	}
	return data[mid];
}
} // namespace

// --- Detector Implementation ---

MADDetector::MADDetector(double threshold) : threshold_(threshold) {
	if (threshold_ <= 0) {
		throw std::invalid_argument("Threshold must be positive.");
	}
}

OutlierResult MADDetector::detect(const core::TimeSeries &ts) {
	const auto &values = ts.getValues();
	if (values.size() < 2) {
		ANOFOX_WARN("MADDetector requires at least 2 data points. Returning no outliers.");
		return {};
	}

	// 1. Calculate the median of the series
	std::vector<double> sorted_values = values;
	double median = calculate_median(sorted_values);

	// 2. Calculate the absolute deviations from the median
	std::vector<double> deviations;
	deviations.reserve(values.size());
	for (double val : values) {
		deviations.push_back(std::abs(val - median));
	}

	// 3. Calculate the median of the deviations (the MAD)
	double mad = calculate_median(deviations);

	// A MAD of 0 is problematic, indicates all points are identical.
	// In this case, no point can be an outlier.
	if (mad == 0.0) {
		ANOFOX_INFO("Median Absolute Deviation is zero. No outliers will be detected.");
		return {};
	}

	// 4. Calculate the modified Z-score and identify outliers
	// The constant 0.6745 is the 75th percentile of the standard normal distribution,
	// used to make MAD a consistent estimator for the standard deviation.
	const double mad_to_std_dev_factor = 0.6745;
	OutlierResult result;
	for (size_t i = 0; i < values.size(); ++i) {
		double modified_z_score = (mad_to_std_dev_factor * std::abs(values[i] - median)) / mad;
		if (modified_z_score > threshold_) {
			result.outlier_indices.push_back(i);
		}
	}

	ANOFOX_INFO("MADDetector found {} outliers.", result.outlier_indices.size());
	return result;
}

// --- Builder Implementation ---

MADDetectorBuilder &MADDetectorBuilder::withThreshold(double threshold) {
	threshold_ = threshold;
	return *this;
}

std::unique_ptr<MADDetector> MADDetectorBuilder::build() {
	ANOFOX_DEBUG("Building MADDetector with threshold {}.", threshold_);
	return std::unique_ptr<MADDetector>(new MADDetector(threshold_));
}

} // namespace anofoxtime::detectors
