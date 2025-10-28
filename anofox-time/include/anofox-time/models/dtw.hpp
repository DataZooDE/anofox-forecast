#pragma once

#include "anofox-time/core/distance_matrix.hpp"
#include "anofox-time/utils/logging.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace anofoxtime::models {

/**
 * @brief Available point-wise metrics for DTW accumulation.
 */
enum class DtwMetric {
	Euclidean,
	Manhattan,
};

class DTWBuilder;

/**
 * @class DTW
 * @brief Dynamic Time Warping distance calculator with optional early-abandon controls.
 *
 * The implementation mirrors the augurs DTW implementation while embracing the builder style
 * used across anofox-time. It supports configurable Sakoe-Chiba windowing, optional maximum
 * distance cut-offs, and pluggable distance metrics.
 */
class DTW {
public:
	friend class DTWBuilder;

	using Series = std::vector<double>;

	DTW(const DTW &) = delete;
	DTW &operator=(const DTW &) = delete;
	DTW(DTW &&) noexcept = default;
	DTW &operator=(DTW &&) noexcept = default;

	/**
	 * @brief Compute the DTW distance between two series.
	 */
	double distance(const Series &lhs, const Series &rhs) const;

	/**
	 * @brief Compute a full pairwise distance matrix for the provided series.
	 *
	 * The returned matrix is symmetric with zeros on the diagonal.
	 */
	core::DistanceMatrix distanceMatrix(const std::vector<Series> &series) const;

	/**
	 * @brief Returns the configured metric as a string identifier.
	 */
	std::string metricName() const;

private:
	DTW(DtwMetric metric,
	    std::optional<std::size_t> window,
	    std::optional<double> max_distance,
	    std::optional<double> lower_bound,
	    std::optional<double> upper_bound);

	double distanceWithEarlyStopping(const Series &lhs, const Series &rhs) const;
	double distanceUnbounded(const Series &lhs, const Series &rhs) const;

	double pointDistance(double a, double b) const;
	double thresholdTransform(double value) const;
	double finalizeDistance(double value) const;
	double lowerBoundKim(const Series &lhs, const Series &rhs) const;
	double upperBoundDiag(const Series &lhs, const Series &rhs) const;

	DtwMetric metric_;
	std::optional<std::size_t> window_;
	std::optional<double> max_distance_;
	std::optional<double> lower_bound_;
	std::optional<double> upper_bound_;

	// Mutable buffers reused across invocations to avoid repeated allocations.
	mutable std::vector<double> prev_buffer_;
	mutable std::vector<double> curr_buffer_;
};

/**
 * @class DTWBuilder
 * @brief Fluent builder for configuring DTW instances.
 */
class DTWBuilder {
public:
	DTWBuilder &withMetric(DtwMetric metric);
	DTWBuilder &withWindow(std::size_t window);
	DTWBuilder &withMaxDistance(double max_distance);
	DTWBuilder &withLowerBound(double lower_bound);
	DTWBuilder &withUpperBound(double upper_bound);

	std::unique_ptr<DTW> build() const;

private:
	DtwMetric metric_ = DtwMetric::Euclidean;
	std::optional<std::size_t> window_;
	std::optional<double> max_distance_;
	std::optional<double> lower_bound_;
	std::optional<double> upper_bound_;
};

} // namespace anofoxtime::models

