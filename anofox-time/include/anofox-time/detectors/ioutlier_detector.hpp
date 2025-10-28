#pragma once

#include "anofox-time/core/time_series.hpp"
#include <vector>

namespace anofoxtime::detectors {

/**
 * @struct OutlierResult
 * @brief Holds the results of an outlier detection operation.
 */
struct OutlierResult {
	/// A vector of indices corresponding to the positions of outliers in the original series.
	std::vector<size_t> outlier_indices;
};

/**
 * @class IOutlierDetector
 * @brief An interface for all outlier detection algorithms.
 */
class IOutlierDetector {
public:
	virtual ~IOutlierDetector() = default;

	/**
	 * @brief Detects outliers in the given time series.
	 * @param ts The time series data to analyze.
	 * @return An OutlierResult object containing the indices of detected outliers.
	 */
	virtual OutlierResult detect(const core::TimeSeries &ts) = 0;

	/**
	 * @brief Gets the name of the outlier detector.
	 * @return A string representing the detector's name.
	 */
	virtual std::string getName() const = 0;
};

} // namespace anofoxtime::detectors
