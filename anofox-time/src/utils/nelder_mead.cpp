#include "anofox-time/utils/nelder_mead.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace anofoxtime::utils {

namespace {

std::vector<std::pair<std::vector<double>, double>> sortSimplex(
    std::vector<std::pair<std::vector<double>, double>> simplex) {
	std::sort(simplex.begin(), simplex.end(),
	          [](const auto &lhs, const auto &rhs) { return lhs.second < rhs.second; });
	return simplex;
}

std::vector<double> centroid(const std::vector<std::pair<std::vector<double>, double>> &simplex) {
	const std::size_t n = simplex.front().first.size();
	std::vector<double> center(n, 0.0);
	const std::size_t count = simplex.size() - 1; // exclude worst

	for (std::size_t i = 0; i < count; ++i) {
		const auto &point = simplex[i].first;
		for (std::size_t j = 0; j < n; ++j) {
			center[j] += point[j];
		}
	}
	for (double &value : center) {
		value /= static_cast<double>(count);
	}
	return center;
}

} // namespace

void NelderMeadOptimizer::enforceBounds(std::vector<double> &point,
                                        const std::vector<double> &lower,
                                        const std::vector<double> &upper) {
	if (lower.empty() && upper.empty()) {
		return;
	}
	for (std::size_t i = 0; i < point.size(); ++i) {
		if (!lower.empty()) {
			point[i] = std::max(lower[i], point[i]);
		}
		if (!upper.empty()) {
			point[i] = std::min(upper[i], point[i]);
		}
	}
}

double NelderMeadOptimizer::simplexSpread(const std::vector<double> &values) {
	const double mean =
	    std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
	double accum = 0.0;
	for (double v : values) {
		const double diff = v - mean;
		accum += diff * diff;
	}
	return std::sqrt(accum / static_cast<double>(values.size()));
}

NelderMeadOptimizer::Result NelderMeadOptimizer::minimize(
    const std::function<double(const std::vector<double> &)> &objective,
    const std::vector<double> &initial,
    const Options &options,
    const std::vector<double> &lower_bounds,
    const std::vector<double> &upper_bounds) const {

	Result result;
	if (initial.empty()) {
		return result;
	}

	const std::size_t n = initial.size();
	std::vector<std::pair<std::vector<double>, double>> simplex;
	simplex.reserve(n + 1);

	// Build initial simplex.
	simplex.emplace_back(initial, objective(initial));
	for (std::size_t i = 0; i < n; ++i) {
		std::vector<double> vertex = initial;
		vertex[i] += options.step;
		enforceBounds(vertex, lower_bounds, upper_bounds);
		simplex.emplace_back(vertex, objective(vertex));
	}
	simplex = sortSimplex(std::move(simplex));

	for (int iter = 0; iter < options.max_iterations; ++iter) {
		result.iterations = iter + 1;
		std::vector<double> values;
		values.reserve(simplex.size());
		for (const auto &item : simplex) {
			values.push_back(item.second);
		}

		if (simplexSpread(values) < options.tolerance) {
			result.converged = true;
			break;
		}

		const auto worst = simplex.back();
		const std::vector<double> center = centroid(simplex);

		auto reflectPoint = center;
		for (std::size_t i = 0; i < n; ++i) {
			reflectPoint[i] += options.alpha * (center[i] - worst.first[i]);
		}
		enforceBounds(reflectPoint, lower_bounds, upper_bounds);
		const double reflectValue = objective(reflectPoint);

		if (reflectValue < simplex.front().second) {
			// Expansion
			auto expandPoint = center;
			for (std::size_t i = 0; i < n; ++i) {
				expandPoint[i] += options.gamma * (reflectPoint[i] - center[i]);
			}
			enforceBounds(expandPoint, lower_bounds, upper_bounds);
			const double expandValue = objective(expandPoint);
			if (expandValue < reflectValue) {
				simplex.back() = {std::move(expandPoint), expandValue};
			} else {
				simplex.back() = {std::move(reflectPoint), reflectValue};
			}
		} else if (reflectValue < simplex[simplex.size() - 2].second) {
			// Accept reflection
			simplex.back() = {std::move(reflectPoint), reflectValue};
		} else {
			// Contraction
			auto contractPoint = worst.first;
			for (std::size_t i = 0; i < n; ++i) {
				contractPoint[i] = center[i] + options.rho * (contractPoint[i] - center[i]);
			}
			enforceBounds(contractPoint, lower_bounds, upper_bounds);
			const double contractValue = objective(contractPoint);

			if (contractValue < worst.second) {
				simplex.back() = {std::move(contractPoint), contractValue};
			} else {
				// Shrink simplex towards best
				const auto best_point = simplex.front().first;
				for (std::size_t i = 1; i < simplex.size(); ++i) {
					for (std::size_t j = 0; j < n; ++j) {
						simplex[i].first[j] =
						    best_point[j] + options.sigma * (simplex[i].first[j] - best_point[j]);
					}
					enforceBounds(simplex[i].first, lower_bounds, upper_bounds);
					simplex[i].second = objective(simplex[i].first);
				}
			}
		}

		simplex = sortSimplex(std::move(simplex));
	}

	result.best = simplex.front().first;
	result.value = simplex.front().second;
	return result;
}

} // namespace anofoxtime::utils

