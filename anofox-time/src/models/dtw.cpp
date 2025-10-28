#include "anofox-time/models/dtw.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

using anofoxtime::core::DistanceMatrix;
using anofoxtime::utils::Logging;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

} // namespace

namespace anofoxtime::models {

namespace {
std::size_t computeWindow(std::optional<std::size_t> window, std::size_t m, std::size_t n) {
	const auto diff = (m > n) ? (m - n) : (n - m);
	if (!window.has_value()) {
		return std::max(m, n);
	}
	return std::max(window.value(), diff);
}
} // namespace

DTW::DTW(DtwMetric metric,
         std::optional<std::size_t> window,
         std::optional<double> max_distance,
         std::optional<double> lower_bound,
         std::optional<double> upper_bound)
    : metric_(metric),
      window_(window),
      max_distance_(max_distance),
      lower_bound_(lower_bound),
      upper_bound_(upper_bound) {}

double DTW::distance(const Series &lhs, const Series &rhs) const {
#ifndef ANOFOX_NO_LOGGING
	auto logger = Logging::getLogger();
	if (logger->should_log(spdlog::level::trace)) {
		logger->trace(
		    "DTW distance start metric={} len(lhs)={} len(rhs)={} window={} max={} lower={} upper={}",
		    metricName(),
		    lhs.size(),
		    rhs.size(),
		    window_ ? static_cast<long long>(*window_) : -1,
		    max_distance_.value_or(-1.0),
		    lower_bound_.value_or(-1.0),
		    upper_bound_.value_or(-1.0));
	}
#endif

	const auto result = distanceWithEarlyStopping(lhs, rhs);

#ifndef ANOFOX_NO_LOGGING
	if (logger->should_log(spdlog::level::trace)) {
		logger->trace("DTW distance result={}", result);
	}
#endif
	return result;
}

DistanceMatrix DTW::distanceMatrix(const std::vector<Series> &series) const {
	const auto n = series.size();
	DistanceMatrix::Matrix matrix(n, std::vector<double>(n, 0.0));

	for (std::size_t i = 0; i < n; ++i) {
		matrix[i][i] = 0.0;
		for (std::size_t j = i + 1; j < n; ++j) {
			const auto dist = distanceWithEarlyStopping(series[i], series[j]);
			matrix[i][j] = dist;
			matrix[j][i] = dist;
		}
	}

	return DistanceMatrix::fromSquare(std::move(matrix));
}

std::string DTW::metricName() const {
	switch (metric_) {
	case DtwMetric::Euclidean:
		return "euclidean";
	case DtwMetric::Manhattan:
		return "manhattan";
	default:
		return "unknown";
	}
}

double DTW::distanceWithEarlyStopping(const Series &lhs, const Series &rhs) const {
	if (lhs.empty() && rhs.empty()) {
		return 0.0;
	}
	if (lhs.empty() || rhs.empty()) {
		return kInf;
	}

	const auto transformed_lower = lower_bound_.has_value()
	                                   ? std::optional<double>(thresholdTransform(*lower_bound_))
	                                   : std::nullopt;
	if (transformed_lower) {
		const double lb = lowerBoundKim(lhs, rhs);
		if (lb >= *transformed_lower) {
			return finalizeDistance(*transformed_lower);
		}
	}

	const auto transformed_upper = upper_bound_.has_value()
	                                   ? std::optional<double>(thresholdTransform(*upper_bound_))
	                                   : std::nullopt;
	if (transformed_upper) {
		const double ub = upperBoundDiag(lhs, rhs);
		if (ub <= *transformed_upper) {
			return finalizeDistance(ub);
		}
	}

	return distanceUnbounded(lhs, rhs);
}

double DTW::distanceUnbounded(const Series &lhs, const Series &rhs) const {
	const Series *outer = &lhs;
	const Series *inner = &rhs;
	if (lhs.size() < rhs.size()) {
		std::swap(outer, inner);
	}

	const auto m = outer->size();
	const auto n = inner->size();
	const auto window = computeWindow(window_, m, n);

	const auto transformed_max =
	    max_distance_.has_value() ? std::optional<double>(thresholdTransform(*max_distance_))
	                              : std::nullopt;

	prev_buffer_.assign(n + 1, kInf);
	curr_buffer_.assign(n + 1, kInf);
	prev_buffer_[0] = 0.0;

	for (std::size_t i = 1; i <= m; ++i) {
		std::fill(curr_buffer_.begin(), curr_buffer_.end(), kInf);

		const std::size_t j_start = (i > window) ? std::max<std::size_t>(1, i - window) : 1;
		const std::size_t j_end = std::min<std::size_t>(n, i + window);

		if (j_start > j_end) {
			std::swap(prev_buffer_, curr_buffer_);
			continue;
		}

		double row_min = kInf;
		for (std::size_t j = j_start; j <= j_end; ++j) {
			const auto cost = pointDistance((*outer)[i - 1], (*inner)[j - 1]);
			const auto best = std::min({prev_buffer_[j], prev_buffer_[j - 1], curr_buffer_[j - 1]});
			curr_buffer_[j] = cost + best;
			row_min = std::min(row_min, curr_buffer_[j]);
		}

		std::swap(prev_buffer_, curr_buffer_);

		if (transformed_max && row_min >= *transformed_max) {
			return max_distance_.value();
		}
	}

	auto final_cost = prev_buffer_[n];
	if (transformed_max) {
		final_cost = std::min(final_cost, *transformed_max);
	}
	return finalizeDistance(final_cost);
}

double DTW::pointDistance(double a, double b) const {
	switch (metric_) {
	case DtwMetric::Euclidean: {
		const double diff = a - b;
		return diff * diff;
	}
	case DtwMetric::Manhattan:
		return std::abs(a - b);
	default:
		return kInf;
	}
}

double DTW::thresholdTransform(double value) const {
	switch (metric_) {
	case DtwMetric::Euclidean:
		return value * value;
	case DtwMetric::Manhattan:
		return value;
	default:
		return value;
	}
}

double DTW::finalizeDistance(double value) const {
	switch (metric_) {
	case DtwMetric::Euclidean:
		return std::sqrt(value);
	case DtwMetric::Manhattan:
		return value;
	default:
		return value;
	}
}

double DTW::lowerBoundKim(const Series &lhs, const Series &rhs) const {
	if (lhs.size() < 2 || rhs.size() < 2) {
		return 0.0;
	}

	const auto lhs_last = lhs.size() - 1;
	const auto rhs_last = rhs.size() - 1;

	double sum = pointDistance(lhs.front(), rhs.front()) +
	             pointDistance(lhs[lhs_last], rhs[rhs_last]);
	if (sum == kInf || lhs.size() < 3 || rhs.size() < 3) {
		return sum;
	}

	sum += std::min(
	    {pointDistance(lhs.front(), rhs[1]),
	     pointDistance(lhs[1], rhs.front()),
	     pointDistance(lhs[1], rhs[1])});

	sum += std::min(
	    {pointDistance(lhs[lhs_last], rhs[rhs_last - 1]),
	     pointDistance(lhs[lhs_last - 1], rhs[rhs_last]),
	     pointDistance(lhs[lhs_last - 1], rhs[rhs_last - 1])});

	return sum;
}

double DTW::upperBoundDiag(const Series &lhs, const Series &rhs) const {
	const auto limit = std::min(lhs.size(), rhs.size());
	double sum = 0.0;
	for (std::size_t i = 0; i < limit; ++i) {
		sum += pointDistance(lhs[i], rhs[i]);
	}
	return sum;
}

DTWBuilder &DTWBuilder::withMetric(DtwMetric metric) {
	metric_ = metric;
	return *this;
}

DTWBuilder &DTWBuilder::withWindow(std::size_t window) {
	window_ = window;
	return *this;
}

DTWBuilder &DTWBuilder::withMaxDistance(double max_distance) {
	if (max_distance < 0.0) {
		throw std::invalid_argument("max_distance must be non-negative");
	}
	max_distance_ = max_distance;
	return *this;
}

DTWBuilder &DTWBuilder::withLowerBound(double lower_bound) {
	if (lower_bound < 0.0) {
		throw std::invalid_argument("lower_bound must be non-negative");
	}
	lower_bound_ = lower_bound;
	return *this;
}

DTWBuilder &DTWBuilder::withUpperBound(double upper_bound) {
	if (upper_bound < 0.0) {
		throw std::invalid_argument("upper_bound must be non-negative");
	}
	upper_bound_ = upper_bound;
	return *this;
}

std::unique_ptr<DTW> DTWBuilder::build() const {
	return std::unique_ptr<DTW>(
	    new DTW(metric_, window_, max_distance_, lower_bound_, upper_bound_));
}

} // namespace anofoxtime::models

