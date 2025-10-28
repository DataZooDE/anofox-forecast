#pragma once

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <vector>

namespace anofoxtime::core {

/**
 * @struct Forecast
 * @brief Holds the results of a forecasting operation.
 *
 * This struct contains the primary point predictions and may optionally include
 * upper and lower prediction intervals for uncertainty estimation.
 */
struct Forecast {
	using Value = double;
	using Series = std::vector<Value>;
	using Matrix = std::vector<Series>;

	/// Point forecasts arranged by dimension (dimension-major).
	Matrix point;

	/// Optional lower bounds of the prediction intervals (dimension-major).
	std::optional<Matrix> lower;

	/// Optional upper bounds of the prediction intervals (dimension-major).
	std::optional<Matrix> upper;

	/// Ensures the forecast contains at least @p dims value dimensions.
	void ensureDimensions(std::size_t dims) {
		if (point.size() < dims) {
			point.resize(dims);
		}
	}

	/// Access (and lazily create) the series for a given dimension.
	Series &series(std::size_t dimension = 0) {
		ensureDimensions(dimension + 1);
		return point[dimension];
	}

	/// Const access to a series for a dimension.
	const Series &series(std::size_t dimension = 0) const {
		if (dimension >= point.size()) {
			static const Series empty_series{};
			return empty_series;
		}
		return point[dimension];
	}

	/// Returns the primary (first) dimension series.
	Series &primary() {
		return series(0);
	}

	/// Const overload for accessing the first series.
	const Series &primary() const {
		return series(0);
	}

	/// Returns whether the forecast contains any values.
	bool empty() const {
		return point.empty() || point.front().empty();
	}

	/// Returns the number of value dimensions.
	std::size_t dimensions() const {
		return point.size();
	}

	/// Returns true when the forecast contains more than one dimension.
	bool isMultivariate() const {
		return dimensions() > 1;
	}

	/// Returns the forecast horizon (number of steps).
	std::size_t horizon() const {
		return empty() ? 0 : point.front().size();
	}

	/// Mutable access to the lower interval matrix, ensuring @p dims slots exist.
	Matrix &ensureLower(std::size_t dims) {
		if (!lower.has_value()) {
			lower.emplace();
		}
		if (lower->size() < dims) {
			lower->resize(dims);
		}
		return *lower;
	}

	/// Mutable access to the upper interval matrix, ensuring @p dims slots exist.
	Matrix &ensureUpper(std::size_t dims) {
		if (!upper.has_value()) {
			upper.emplace();
		}
		if (upper->size() < dims) {
			upper->resize(dims);
		}
		return *upper;
	}

	/// Access (and create when needed) the lower interval for a dimension.
	Series &lowerSeries(std::size_t dimension = 0) {
		auto dims = std::max(dimensions(), dimension + 1);
		return ensureLower(dims)[dimension];
	}

	/// Access (and create when needed) the upper interval for a dimension.
	Series &upperSeries(std::size_t dimension = 0) {
		auto dims = std::max(dimensions(), dimension + 1);
		return ensureUpper(dims)[dimension];
	}

	/// Const access to the lower interval for a dimension.
	const Series &lowerSeries(std::size_t dimension = 0) const {
		if (!lower.has_value() || dimension >= lower->size()) {
			throw std::out_of_range("Lower interval not available for requested dimension.");
		}
		return (*lower)[dimension];
	}

	/// Const access to the upper interval for a dimension.
	const Series &upperSeries(std::size_t dimension = 0) const {
		if (!upper.has_value() || dimension >= upper->size()) {
			throw std::out_of_range("Upper interval not available for requested dimension.");
		}
		return (*upper)[dimension];
	}
};

} // namespace anofoxtime::core
