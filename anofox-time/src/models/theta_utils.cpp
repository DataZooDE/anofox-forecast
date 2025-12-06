#include "anofox-time/models/theta_utils.hpp"
#include "anofox-time/utils/logging.hpp"
#include <numeric>
#include <algorithm>

namespace anofoxtime::models::theta_utils {

namespace {
    constexpr double kEpsilon = 1e-10;
}

void deseasonalize(const std::vector<double>& data,
                   int seasonal_period,
                   std::vector<double>& deseasonalized,
                   std::vector<double>& seasonal_indices) {
    const size_t n = data.size();
    
    // If no seasonality, just copy data
    if (seasonal_period <= 1) {
        deseasonalized = data;
        seasonal_indices.clear();
        return;
    }
    
    // Check if we have enough data
    if (n < 2 * static_cast<size_t>(seasonal_period)) {
        ANOFOX_WARN("Theta: Insufficient data for seasonal decomposition");
        deseasonalized = data;
        seasonal_indices.clear();
        return;
    }
    
    // Compute centered moving average for trend
    std::vector<double> trend(n, 0.0);
    const int half_period = seasonal_period / 2;
    const bool is_even = (seasonal_period % 2 == 0);
    
    for (size_t i = static_cast<size_t>(seasonal_period); 
         i < n - static_cast<size_t>(seasonal_period); ++i) {
        double sum = 0.0;
        if (is_even) {
            sum += 0.5 * data[i - half_period];
            for (int j = 1 - half_period; j < half_period; ++j) {
                sum += data[i + j];
            }
            sum += 0.5 * data[i + half_period];
            trend[i] = sum / static_cast<double>(seasonal_period);
        } else {
            for (int j = -half_period; j <= half_period; ++j) {
                sum += data[i + j];
            }
            trend[i] = sum / static_cast<double>(seasonal_period);
        }
    }
    
    // Compute seasonal indices
    std::vector<std::vector<double>> seasonal_obs(seasonal_period);
    // Reserve space to avoid reallocations
    size_t expected_obs_per_season = (n - 2 * seasonal_period) / seasonal_period + 1;
    for (int s = 0; s < seasonal_period; ++s) {
        seasonal_obs[s].reserve(expected_obs_per_season);
    }
    
    for (size_t i = static_cast<size_t>(seasonal_period); 
         i < n - static_cast<size_t>(seasonal_period); ++i) {
        if (trend[i] > kEpsilon) {
            size_t season_idx = i % static_cast<size_t>(seasonal_period);
            seasonal_obs[season_idx].push_back(data[i] / trend[i]);
        }
    }
    
    // Average seasonal indices
    seasonal_indices.resize(seasonal_period, 1.0);
    for (int s = 0; s < seasonal_period; ++s) {
        if (!seasonal_obs[s].empty()) {
            double sum = std::accumulate(seasonal_obs[s].begin(), seasonal_obs[s].end(), 0.0);
            seasonal_indices[s] = sum / static_cast<double>(seasonal_obs[s].size());
        }
    }
    
    // Normalize
    double avg_index = std::accumulate(seasonal_indices.begin(), 
                                       seasonal_indices.end(), 0.0) / 
                       static_cast<double>(seasonal_period);
    if (avg_index > kEpsilon) {
        for (double& idx : seasonal_indices) {
            idx /= avg_index;
        }
    }
    
    // Deseasonalize
    deseasonalized.resize(n);
    for (size_t i = 0; i < n; ++i) {
        size_t season_idx = i % static_cast<size_t>(seasonal_period);
        if (seasonal_indices[season_idx] > kEpsilon) {
            deseasonalized[i] = data[i] / seasonal_indices[season_idx];
        } else {
            deseasonalized[i] = data[i];
        }
    }
}

void reseasonalize(const std::vector<double>& forecast,
                   const std::vector<double>& seasonal_indices,
                   int seasonal_period,
                   size_t history_size,
                   std::vector<double>& reseasonalized) {
    if (seasonal_period <= 1 || seasonal_indices.empty()) {
        reseasonalized = forecast;
        return;
    }
    
    reseasonalized.resize(forecast.size());
    
    for (size_t h = 0; h < forecast.size(); ++h) {
        size_t season_idx = (history_size + h) % static_cast<size_t>(seasonal_period);
        reseasonalized[h] = forecast[h] * seasonal_indices[season_idx];
    }
}

} // namespace anofoxtime::models::theta_utils








