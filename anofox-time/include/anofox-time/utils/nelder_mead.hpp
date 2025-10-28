#pragma once

#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace anofoxtime::utils {

class NelderMeadOptimizer {
public:
	struct Options {
		double alpha = 1.0;      // reflection
		double gamma = 2.0;      // expansion
		double rho = 0.5;        // contraction
		double sigma = 0.5;      // shrink
		double step = 0.05;      // initial simplex step
		int max_iterations = 500;
		double tolerance = 1e-6;
	};

	struct Result {
		std::vector<double> best;
		double value = std::numeric_limits<double>::quiet_NaN();
		int iterations = 0;
		bool converged = false;
	};

	Result minimize(const std::function<double(const std::vector<double> &)> &objective,
	                const std::vector<double> &initial,
	                const Options &options,
	                const std::vector<double> &lower_bounds = {},
	                const std::vector<double> &upper_bounds = {}) const;

private:
	static void enforceBounds(std::vector<double> &point,
	                          const std::vector<double> &lower,
	                          const std::vector<double> &upper);

	static double simplexSpread(const std::vector<double> &values);
};

} // namespace anofoxtime::utils
