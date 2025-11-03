#pragma once

#include <vector>

namespace anofoxtime::utils {

/**
 * @brief Robust regression utilities
 *
 * Provides outlier-resistant regression methods for time series analysis
 */
namespace RobustRegression {

/**
 * @brief Siegel Repeated Medians Regression
 *
 * Computes robust linear regression using repeated medians of slopes.
 * This method is highly resistant to outliers compared to OLS.
 *
 * Algorithm:
 * 1. For each point i, compute slopes to all other points j
 * 2. Take median of slopes for each point
 * 3. Overall slope = median of all point-wise median slopes
 * 4. Compute intercepts and take median intercept
 *
 * @param x Independent variable (e.g., time indices)
 * @param y Dependent variable (e.g., time series values)
 * @param slope Output: robust slope estimate
 * @param intercept Output: robust intercept estimate
 *
 * Reference: statsforecast MFLES siegel_repeated_medians()
 */
void siegelRepeatedMedians(const std::vector<double>& x,
                           const std::vector<double>& y,
                           double& slope,
                           double& intercept);

/**
 * @brief Compute median of a vector
 *
 * @param data Input data (will be modified for partial_sort)
 * @return Median value
 */
double median(std::vector<double>& data);

} // namespace RobustRegression
} // namespace anofoxtime::utils
