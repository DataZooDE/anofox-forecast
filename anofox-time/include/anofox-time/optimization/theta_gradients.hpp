#pragma once

#include <vector>
#include <cmath>
#include "anofox-time/models/theta_pegels.hpp"

namespace anofoxtime::optimization {

/**
 * @brief Gradient computation for Theta method objective function
 * 
 * Computes numerical gradients of the MSE objective with respect to
 * optimization parameters (level, alpha, theta).
 * 
 * This enables gradient-based optimization (L-BFGS) for parameter estimation.
 */
class ThetaGradients {
public:
    /**
     * @brief Workspace for gradient computation to avoid repeated allocations
     */
    struct Workspace {
        models::theta_pegels::StateMatrix states;
        std::vector<double> e;
        std::vector<double> amse;
        
        void resize(size_t n, size_t nmse) {
            if (states.size() < n) states.resize(n);
            if (e.size() < n) e.resize(n);
            if (amse.size() < nmse) amse.resize(nmse);
        }
    };
    
    /**
     * @brief Compute MSE and gradients for Theta optimization
     * 
     * @param y Time series data
     * @param model_type Theta model variant (STM, OTM, DSTM, DOTM)
     * @param level Initial level parameter
     * @param alpha Smoothing parameter
     * @param theta Theta parameter
     * @param opt_level Whether to compute gradient w.r.t. level
     * @param opt_alpha Whether to compute gradient w.r.t. alpha
     * @param opt_theta Whether to compute gradient w.r.t. theta
     * @param nmse Number of steps for multi-step MSE
     * @param gradients Output: gradient vector [d_level, d_alpha, d_theta]
     * @param workspace Pre-allocated workspace for computations
     * @return double MSE value
     */
    static double computeMSEWithGradients(
        const std::vector<double>& y,
        models::theta_pegels::ModelType model_type,
        double level,
        double alpha,
        double theta,
        bool opt_level,
        bool opt_alpha,
        bool opt_theta,
        size_t nmse,
        std::vector<double>& gradients,
        Workspace& workspace
    );
    
private:
    /**
     * @brief Compute MSE for given parameters
     * 
     * Helper function to evaluate objective at a specific parameter point.
     */
    static double computeMSE(
        const std::vector<double>& y,
        models::theta_pegels::ModelType model_type,
        double level,
        double alpha,
        double theta,
        size_t nmse,
        Workspace& workspace
    );
    
    /**
     * @brief Compute numerical gradient using finite differences
     * 
     * Uses forward differences for numerical gradient estimation.
     * Adaptive step size based on parameter magnitude.
     */
    static double numericalGradient(
        const std::vector<double>& y,
        models::theta_pegels::ModelType model_type,
        double level,
        double alpha,
        double theta,
        double base_mse,
        int param_idx,  // 0=level, 1=alpha, 2=theta
        size_t nmse,
        Workspace& workspace
    );
};

} // namespace anofoxtime::optimization

