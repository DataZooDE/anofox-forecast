#pragma once

#include <vector>
#include <cstddef>
#include "anofox-time/models/ets.hpp"

namespace anofoxtime::optimization {

/**
 * @brief Gradient checkpointing for memory-efficient ETS backpropagation
 * 
 * Instead of storing all intermediate states (O(n*m) memory),
 * we store only checkpoints and recompute states as needed.
 * 
 * Trade-off: ~20% more computation for 80-90% less memory
 */
class ETSGradientsCheckpointing {
public:
    struct Checkpoint {
        size_t timestep;
        double level;
        double trend;
        std::vector<double> seasonals;
    };
    
    struct CheckpointConfig {
        size_t checkpoint_interval = 50;  // Store every N steps
        bool enabled = true;
        size_t min_series_length = 200;  // Only use for long series
    };
    
    /**
     * @brief Decide whether to use checkpointing based on series length
     */
    static bool shouldUseCheckpointing(size_t n, const CheckpointConfig& config);
    
    /**
     * @brief Create checkpoints during forward pass
     * 
     * @param config ETS configuration
     * @param values Time series data
     * @param level0 Initial level
     * @param trend0 Initial trend
     * @param seasonal0 Initial seasonal states
     * @param checkpoint_config Checkpointing configuration
     * @return std::vector<Checkpoint> Checkpoints at regular intervals
     */
    static std::vector<Checkpoint> createCheckpoints(
        const models::ETSConfig& config,
        const std::vector<double>& values,
        double level0,
        double trend0,
        const std::vector<double>& seasonal0,
        const CheckpointConfig& checkpoint_config
    );
    
    /**
     * @brief Recompute states between checkpoints for backward pass
     * 
     * When backprop needs states at time t, find nearest checkpoint
     * and recompute forward from there to t.
     * 
     * @param checkpoints Stored checkpoints
     * @param config ETS configuration
     * @param values Time series data
     * @param target_time Target timestep
     * @return Checkpoint State at target_time
     */
    static Checkpoint recomputeFromCheckpoint(
        const std::vector<Checkpoint>& checkpoints,
        const models::ETSConfig& config,
        const std::vector<double>& values,
        size_t target_time
    );
    
private:
    // Helper: Find nearest checkpoint before target_time
    static size_t findNearestCheckpoint(
        const std::vector<Checkpoint>& checkpoints,
        size_t target_time
    );
    
    // Helper: Run forward pass from checkpoint to target
    static void forwardStep(
        const models::ETSConfig& config,
        double observation,
        double& level,
        double& trend,
        std::vector<double>& seasonals,
        size_t season_index
    );
};

} // namespace anofoxtime::optimization



