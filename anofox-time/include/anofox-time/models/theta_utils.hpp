#pragma once

#include <vector>
#include <cstddef>

namespace anofoxtime::models::theta_utils {

/**
 * Deseasonalize time series data using centered moving average
 * 
 * @param data Input time series
 * @param seasonal_period Seasonal period length
 * @param deseasonalized Output deseasonalized series (resized automatically)
 * @param seasonal_indices Output seasonal indices (resized automatically)
 */
void deseasonalize(const std::vector<double>& data,
                   int seasonal_period,
                   std::vector<double>& deseasonalized,
                   std::vector<double>& seasonal_indices);

/**
 * Reseasonalize forecast using seasonal indices
 * 
 * @param forecast Input forecast values
 * @param seasonal_indices Seasonal indices
 * @param seasonal_period Seasonal period length
 * @param history_size Size of historical data (used to compute season offset)
 * @param reseasonalized Output reseasonalized forecast (resized automatically)
 */
void reseasonalize(const std::vector<double>& forecast,
                   const std::vector<double>& seasonal_indices,
                   int seasonal_period,
                   size_t history_size,
                   std::vector<double>& reseasonalized);

} // namespace anofoxtime::models::theta_utils




