#include "anofox-time/optimization/theta_gradients.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace anofoxtime::optimization {

double ThetaGradients::computeMSE(
    const std::vector<double>& y,
    models::theta_pegels::ModelType model_type,
    double level,
    double alpha,
    double theta,
    size_t nmse,
    Workspace& workspace) {
    
    // Ensure workspace is sized correctly
    workspace.resize(y.size(), nmse);
    
    double mse = models::theta_pegels::calc(y, workspace.states, model_type, 
                                           level, alpha, theta, 
                                           workspace.e, workspace.amse, nmse);
    
    if (std::isnan(mse) || mse < 0.0) {
        return std::numeric_limits<double>::infinity();
    }
    
    return mse;
}

double ThetaGradients::numericalGradient(
    const std::vector<double>& y,
    models::theta_pegels::ModelType model_type,
    double level,
    double alpha,
    double theta,
    double base_mse,
    int param_idx,
    size_t nmse,
    Workspace& workspace) {
    
    // Adaptive epsilon based on parameter magnitude and type
    double eps;
    double param_val;
    
    switch (param_idx) {
        case 0: // level
            param_val = level;
            eps = std::max(1e-5, std::abs(level) * 1e-5);
            break;
        case 1: // alpha
            param_val = alpha;
            eps = 1e-5;  // Fixed for alpha since it's in [0,1]
            break;
        case 2: // theta
            param_val = theta;
            eps = std::max(1e-5, std::abs(theta) * 1e-5);
            break;
        default:
            return 0.0;
    }
    
    // Ensure epsilon doesn't push us out of bounds
    if (param_idx == 1) {  // alpha
        eps = std::min(eps, 0.99 - alpha);
    } else if (param_idx == 2) {  // theta
        eps = std::min(eps, 10.0 - theta);
    }
    
    // Perturb parameter
    double level_plus = level;
    double alpha_plus = alpha;
    double theta_plus = theta;
    
    switch (param_idx) {
        case 0: level_plus += eps; break;
        case 1: alpha_plus += eps; break;
        case 2: theta_plus += eps; break;
    }
    
    // Compute MSE at perturbed point
    double mse_plus = computeMSE(y, model_type, level_plus, alpha_plus, theta_plus, nmse, workspace);
    
    // Handle infinite or invalid MSE
    if (!std::isfinite(mse_plus)) {
        // Try backward difference if forward fails
        switch (param_idx) {
            case 0: level_plus = level - eps; break;
            case 1: alpha_plus = alpha - eps; break;
            case 2: theta_plus = theta - eps; break;
        }
        mse_plus = computeMSE(y, model_type, level_plus, alpha_plus, theta_plus, nmse, workspace);
        if (!std::isfinite(mse_plus)) {
            return 0.0;  // Gradient unavailable
        }
        return (base_mse - mse_plus) / eps;  // Backward difference
    }
    
    // Forward difference
    return (mse_plus - base_mse) / eps;
}

double ThetaGradients::computeMSEWithGradients(
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
    Workspace& workspace) {
    
    // Compute base MSE
    double base_mse = computeMSE(y, model_type, level, alpha, theta, nmse, workspace);
    
    if (!std::isfinite(base_mse)) {
        std::fill(gradients.begin(), gradients.end(), 0.0);
        return base_mse;
    }
    
    // Compute gradients for requested parameters
    size_t grad_idx = 0;
    
    if (opt_level) {
        gradients[grad_idx++] = numericalGradient(y, model_type, level, alpha, theta,
                                                   base_mse, 0, nmse, workspace);
    }
    
    if (opt_alpha) {
        gradients[grad_idx++] = numericalGradient(y, model_type, level, alpha, theta,
                                                   base_mse, 1, nmse, workspace);
    }
    
    if (opt_theta) {
        gradients[grad_idx++] = numericalGradient(y, model_type, level, alpha, theta,
                                                   base_mse, 2, nmse, workspace);
    }
    
    return base_mse;
}

} // namespace anofoxtime::optimization

