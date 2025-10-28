#include "anofox-time/optimization/ets_gradients_checkpointing.hpp"
#include <algorithm>
#include <cmath>

namespace anofoxtime::optimization {

namespace {
constexpr double kEpsilon = 1e-8;
constexpr double kPositiveFloor = 1e-6;

inline double clampPositive(double value) {
    return std::max(value, kPositiveFloor);
}

inline double safeDivide(double num, double denom) {
    if (std::abs(denom) < kEpsilon) {
        denom = (denom >= 0.0) ? kEpsilon : -kEpsilon;
    }
    return num / denom;
}

inline double clamp(double value, double lower, double upper) {
    return std::max(lower, std::min(value, upper));
}
}

bool ETSGradientsCheckpointing::shouldUseCheckpointing(
    size_t n, 
    const CheckpointConfig& config
) {
    if (!config.enabled) return false;
    if (n < config.min_series_length) return false;
    
    // For very long series, checkpointing saves significant memory
    // Memory without checkpointing: O(n * m) where m = season_length
    // Memory with checkpointing: O(n/checkpoint_interval * m)
    return true;
}

std::vector<ETSGradientsCheckpointing::Checkpoint> ETSGradientsCheckpointing::createCheckpoints(
    const models::ETSConfig& config,
    const std::vector<double>& values,
    double level0,
    double trend0,
    const std::vector<double>& seasonal0,
    const CheckpointConfig& checkpoint_config
) {
    const size_t n = values.size();
    std::vector<Checkpoint> checkpoints;
    
    // Always store initial state
    Checkpoint initial;
    initial.timestep = 0;
    initial.level = level0;
    initial.trend = trend0;
    initial.seasonals = seasonal0;
    checkpoints.push_back(initial);
    
    // If checkpointing disabled or series too short, just return initial
    if (!shouldUseCheckpointing(n, checkpoint_config)) {
        return checkpoints;
    }
    
    // Run forward pass and store checkpoints at intervals
    double level = level0;
    double trend = trend0;
    std::vector<double> seasonals = seasonal0;
    const size_t m = seasonals.empty() ? 1 : seasonals.size();
    
    for (size_t t = 0; t < n; ++t) {
        // Store checkpoint at intervals
        if (t > 0 && t % checkpoint_config.checkpoint_interval == 0) {
            Checkpoint cp;
            cp.timestep = t;
            cp.level = level;
            cp.trend = trend;
            cp.seasonals = seasonals;
            checkpoints.push_back(cp);
        }
        
        // Forward step
        forwardStep(config, values[t], level, trend, seasonals, t % m);
    }
    
    // Store final state as checkpoint
    Checkpoint final_cp;
    final_cp.timestep = n;
    final_cp.level = level;
    final_cp.trend = trend;
    final_cp.seasonals = seasonals;
    checkpoints.push_back(final_cp);
    
    return checkpoints;
}

ETSGradientsCheckpointing::Checkpoint ETSGradientsCheckpointing::recomputeFromCheckpoint(
    const std::vector<Checkpoint>& checkpoints,
    const models::ETSConfig& config,
    const std::vector<double>& values,
    size_t target_time
) {
    // Find nearest checkpoint before or at target_time
    size_t cp_idx = findNearestCheckpoint(checkpoints, target_time);
    const Checkpoint& start_cp = checkpoints[cp_idx];
    
    // If checkpoint is exactly at target, return it
    if (start_cp.timestep == target_time) {
        return start_cp;
    }
    
    // Otherwise, recompute forward from checkpoint to target
    Checkpoint result;
    result.timestep = target_time;
    result.level = start_cp.level;
    result.trend = start_cp.trend;
    result.seasonals = start_cp.seasonals;
    
    const size_t m = result.seasonals.empty() ? 1 : result.seasonals.size();
    
    // Recompute steps from checkpoint to target
    for (size_t t = start_cp.timestep; t < target_time; ++t) {
        forwardStep(config, values[t], result.level, result.trend, result.seasonals, t % m);
    }
    
    return result;
}

size_t ETSGradientsCheckpointing::findNearestCheckpoint(
    const std::vector<Checkpoint>& checkpoints,
    size_t target_time
) {
    // Binary search for nearest checkpoint <= target_time
    size_t left = 0;
    size_t right = checkpoints.size();
    
    while (left < right - 1) {
        size_t mid = (left + right) / 2;
        if (checkpoints[mid].timestep <= target_time) {
            left = mid;
        } else {
            right = mid;
        }
    }
    
    return left;
}

void ETSGradientsCheckpointing::forwardStep(
    const models::ETSConfig& config,
    double observation,
    double& level,
    double& trend,
    std::vector<double>& seasonals,
    size_t season_index
) {
    const bool has_trend = config.trend != models::ETSTrendType::None;
    const bool has_season = config.season != models::ETSSeasonType::None;
    const bool error_additive = config.error == models::ETSErrorType::Additive;
    const bool season_additive = config.season == models::ETSSeasonType::Additive;
    const bool season_multiplicative = config.season == models::ETSSeasonType::Multiplicative;
    
    // Compute base (level + trend component)
    double base = level;
    if (config.trend == models::ETSTrendType::Additive) {
        base += trend;
    } else if (config.trend == models::ETSTrendType::Multiplicative) {
        base *= clamp(trend, 0.01, 10.0);
    } else if (config.trend == models::ETSTrendType::DampedAdditive) {
        base += config.phi * trend;
    } else if (config.trend == models::ETSTrendType::DampedMultiplicative) {
        base *= std::pow(clamp(trend, 0.01, 10.0), config.phi);
    }
    
    // Add seasonal component
    double fitted = base;
    double seasonal = 0.0;
    if (has_season && !seasonals.empty()) {
        seasonal = seasonals[season_index];
        if (season_additive) {
            fitted = base + seasonal;
        } else if (season_multiplicative) {
            fitted = base * seasonal;
        }
    }
    
    fitted = clampPositive(fitted);
    
    // Compute innovation
    double innovation;
    if (error_additive) {
        innovation = observation - fitted;
    } else {
        innovation = safeDivide(observation, fitted) - 1.0;
        innovation = clamp(innovation, -0.999, 1e6);
    }
    
    // Update states
    double new_level = level;
    double new_trend = trend;
    double new_seasonal = seasonal;
    
    if (error_additive) {
        new_level = base + config.alpha * innovation;
        
        if (has_trend && config.beta) {
            if (config.trend == models::ETSTrendType::Additive) {
                new_trend = trend + (*config.beta) * innovation;
            } else if (config.trend == models::ETSTrendType::DampedAdditive) {
                new_trend = config.phi * trend + (*config.beta) * innovation;
            }
        }
        
        if (has_season && config.gamma) {
            if (season_additive) {
                new_seasonal = seasonal + (*config.gamma) * innovation;
            } else if (season_multiplicative) {
                double season_update = 1.0 + (*config.gamma) * safeDivide(innovation, base);
                new_seasonal = clamp(seasonal * season_update, 0.1, 10.0);
            }
        }
    } else {
        // Multiplicative error
        new_level = base * (1.0 + config.alpha * innovation);
        const double scale = base * innovation;
        
        if (has_trend && config.beta) {
            if (config.trend == models::ETSTrendType::Additive) {
                new_trend = trend + (*config.beta) * scale;
            } else if (config.trend == models::ETSTrendType::DampedAdditive) {
                new_trend = config.phi * trend + (*config.beta) * scale;
            }
        }
        
        if (has_season && config.gamma) {
            if (season_additive) {
                new_seasonal = seasonal + (*config.gamma) * scale;
            } else if (season_multiplicative) {
                new_seasonal = clamp(seasonal * (1.0 + (*config.gamma) * innovation), 0.1, 10.0);
            }
        }
    }
    
    // Apply updates
    level = new_level;
    if (has_trend) {
        trend = new_trend;
    }
    if (has_season && !seasonals.empty()) {
        seasonals[season_index] = new_seasonal;
    }
}

} // namespace anofoxtime::optimization



