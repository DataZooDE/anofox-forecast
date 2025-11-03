#include "anofox-time/utils/robust_regression.hpp"
#include <algorithm>
#include <stdexcept>
#include <cmath>

namespace anofoxtime::utils {

namespace RobustRegression {

double median(std::vector<double>& data) {
	if (data.empty()) {
		throw std::invalid_argument("Cannot compute median of empty vector");
	}

	size_t n = data.size();
	size_t mid = n / 2;

	// Use nth_element for O(n) median (partial sort)
	std::nth_element(data.begin(), data.begin() + mid, data.end());

	if (n % 2 == 1) {
		// Odd number of elements
		return data[mid];
	} else {
		// Even number of elements: average of two middle values
		double mid_val = data[mid];
		// Find the maximum of the lower half
		auto lower_max = *std::max_element(data.begin(), data.begin() + mid);
		return (lower_max + mid_val) / 2.0;
	}
}

void siegelRepeatedMedians(const std::vector<double>& x,
                           const std::vector<double>& y,
                           double& slope,
                           double& intercept) {
	size_t n = x.size();

	if (n != y.size()) {
		throw std::invalid_argument("x and y must have same size");
	}

	if (n < 2) {
		throw std::invalid_argument("Need at least 2 points for regression");
	}

	// Storage for slope calculations
	std::vector<double> slopes(n);
	std::vector<double> slopes_sub(n - 1);

	// For each point i, compute median of slopes to all other points
	for (size_t i = 0; i < n; ++i) {
		size_t k = 0;
		for (size_t j = 0; j < n; ++j) {
			if (i == j) {
				continue;
			}

			double xd = x[j] - x[i];
			double slope_ij;

			if (std::abs(xd) < 1e-10) {
				// Avoid division by zero
				slope_ij = 0.0;
			} else {
				slope_ij = (y[j] - y[i]) / xd;
			}

			slopes_sub[k] = slope_ij;
			++k;
		}

		// Median of slopes for point i
		slopes[i] = median(slopes_sub);
	}

	// Overall slope: median of all point-wise median slopes
	slope = median(slopes);

	// Compute intercepts using the median slope
	std::vector<double> intercepts(n);
	for (size_t i = 0; i < n; ++i) {
		intercepts[i] = y[i] - slope * x[i];
	}

	// Overall intercept: median of all intercepts
	intercept = median(intercepts);
}

} // namespace RobustRegression
} // namespace anofoxtime::utils
