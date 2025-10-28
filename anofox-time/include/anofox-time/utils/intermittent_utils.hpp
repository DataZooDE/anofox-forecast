#pragma once

#include <vector>
#include <cmath>
#include <utility>

namespace anofoxtime::utils {

/**
 * @brief Utility functions for intermittent demand forecasting
 * 
 * These functions are used by Croston, TSB, ADIDA, and IMAPA methods
 * to handle sparse time series with many zero observations.
 */
namespace intermittent {

/**
 * @brief Extract non-zero values from time series (demand values)
 * @param y Input time series
 * @return Vector containing only positive values
 */
std::vector<double> extractDemand(const std::vector<double>& y);

/**
 * @brief Compute intervals between non-zero elements
 * @param y Input time series
 * @return Vector of inter-demand intervals (time between non-zero observations)
 */
std::vector<double> computeIntervals(const std::vector<double>& y);

/**
 * @brief Compute binary probability indicators (1 if non-zero, 0 otherwise)
 * @param y Input time series
 * @return Vector of 0/1 indicators
 */
std::vector<double> computeProbability(const std::vector<double>& y);

/**
 * @brief Simple Exponential Smoothing forecast with fitted values
 * @param x Input series
 * @param alpha Smoothing parameter (0 to 1)
 * @return Pair of (one-step-ahead forecast, fitted values vector)
 */
std::pair<double, std::vector<double>> sesForecasting(const std::vector<double>& x, double alpha);

/**
 * @brief Optimized SES forecast using Nelder-Mead optimization
 * @param x Input series
 * @param lower_bound Lower bound for alpha optimization
 * @param upper_bound Upper bound for alpha optimization
 * @return Pair of (one-step-ahead forecast, fitted values vector)
 */
std::pair<double, std::vector<double>> optimizedSesForecasting(
    const std::vector<double>& x,
    double lower_bound = 0.1,
    double upper_bound = 0.3
);

/**
 * @brief Split array into chunks and sum each chunk
 * @param array Input array
 * @param chunk_size Size of each chunk
 * @return Vector of chunk sums (incomplete chunks discarded)
 */
std::vector<double> chunkSums(const std::vector<double>& array, int chunk_size);

/**
 * @brief Expand fitted demand values to match original time series length
 * @param fitted Fitted values for demand occurrences
 * @param y Original time series
 * @return Expanded fitted values with NaN for zero positions
 */
std::vector<double> expandFittedDemand(
    const std::vector<double>& fitted,
    const std::vector<double>& y
);

/**
 * @brief Expand fitted interval values to match original time series length
 * @param fitted Fitted values for intervals
 * @param y Original time series
 * @return Expanded fitted interval values
 */
std::vector<double> expandFittedIntervals(
    const std::vector<double>& fitted,
    const std::vector<double>& y
);

/**
 * @brief Compute one-step-ahead forecast at aggregation level
 * @param y Input time series
 * @param aggregation_level Temporal aggregation level
 * @return Forecast value
 */
double chunkForecast(const std::vector<double>& y, int aggregation_level);

} // namespace intermittent
} // namespace anofoxtime::utils


