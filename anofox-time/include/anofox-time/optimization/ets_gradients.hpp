#pragma once

#include <vector>
#include <cmath>
#include "anofox-time/models/ets.hpp"

namespace anofoxtime::optimization {

/**
 * @brief Gradient computation for ETS likelihood function
 * 
 * Computes analytical gradients of the negative log-likelihood with respect to
 * smoothing parameters (alpha, beta, gamma, phi) and initial states (level, trend).
 * 
 * This enables gradient-based optimization (L-BFGS) for parameter estimation.
 */
class ETSGradients {
public:
    struct GradientComponents {
        double d_alpha = 0.0;
        double d_beta = 0.0;
        double d_gamma = 0.0;
        double d_phi = 0.0;
        double d_level = 0.0;
        double d_trend = 0.0;
    };
    
    /**
     * @brief Compute gradients of negative log-likelihood
     * 
     * @param config ETS configuration
     * @param values Time series data
     * @param level Initial level
     * @param trend Initial trend (if has_trend)
     * @param seasonals Initial seasonal components (if has_season)
     * @param gradients Output: computed gradients
     * @return double Negative log-likelihood value
     */
    static double computeNegLogLikelihoodWithGradients(
        const models::ETSConfig& config,
        const std::vector<double>& values,
        double level,
        double trend,
        const std::vector<double>& seasonals,
        GradientComponents& gradients
    );
    
private:
    // Forward pass: compute fitted values and innovations
    struct ForwardPass {
        std::vector<double> fitted;
        std::vector<double> innovations;
        std::vector<double> levels;
        std::vector<double> trends;
        std::vector<std::vector<double>> seasonal_states;
        double innovation_sse;
        double sum_log_forecast;
    };
    
    static ForwardPass runForward(
        const models::ETSConfig& config,
        const std::vector<double>& values,
        double level0,
        double trend0,
        const std::vector<double>& seasonal0
    );
    
    // Backward pass: compute gradients via backpropagation
    static void runBackward(
        const models::ETSConfig& config,
        const std::vector<double>& values,
        const ForwardPass& forward,
        GradientComponents& gradients
    );
    
    // Helper: compute gradient of state update equations
    static void computeStateGradients(
        const models::ETSConfig& config,
        double observation,
        double fitted,
        double innovation,
        double level,
        double trend,
        double seasonal,
        GradientComponents& d_state
    );
};

} // namespace anofoxtime::optimization

