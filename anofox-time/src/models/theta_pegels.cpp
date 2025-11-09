#include "anofox-time/models/theta_pegels.hpp"
#include "anofox-time/utils/nelder_mead.hpp"
#include "anofox-time/optimization/lbfgs_optimizer.hpp"
#include "anofox-time/optimization/theta_gradients.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace anofoxtime::models::theta_pegels {

State init_state(const std::vector<double>& y,
                 ModelType model_type,
                 double initial_smoothed,
                 double alpha,
                 double theta) {
    // Port of statsforecast theta_cpp.txt lines 20-38
    double An, Bn, mu;
    
    if (model_type == ModelType::DSTM || model_type == ModelType::DOTM) {
        // Dynamic models: initialize with first value
        An = y[0];
        Bn = 0.0;
        mu = y[0];
    } else {
        // Static models: compute An, Bn from full data
        size_t n = y.size();
        
        // Compute mean
        double y_mean = std::accumulate(y.begin(), y.end(), 0.0) / static_cast<double>(n);
        
        // Compute weighted average: sum(i * y[i]) / n
        double weighted_avg = 0.0;
        for (size_t i = 0; i < n; ++i) {
            weighted_avg += static_cast<double>(i + 1) * y[i];
        }
        weighted_avg /= static_cast<double>(n);
        
        // Compute Bn (slope)
        Bn = (6.0 * (2.0 * weighted_avg - (static_cast<double>(n) + 1.0) * y_mean)) / 
             (static_cast<double>(n) * static_cast<double>(n) - 1.0);
        
        // Compute An (intercept)
        An = y_mean - (static_cast<double>(n) + 1.0) * Bn / 2.0;
        
        // Compute mu
        mu = initial_smoothed + (1.0 - 1.0 / theta) * (An + Bn);
    }
    
    // Return state: [level, meany, An, Bn, mu]
    State state;
    state[0] = alpha * y[0] + (1.0 - alpha) * initial_smoothed;  // level
    state[1] = y[0];                                              // meany
    state[2] = An;
    state[3] = Bn;
    state[4] = mu;
    
    return state;
}

void update(StateMatrix& states,
           size_t i,
           ModelType model_type,
           double alpha,
           double theta,
           double y,
           bool usemu) {
    // Port of statsforecast theta_cpp.txt lines 40-61
    
    double level = states[i - 1][0];
    double meany = states[i - 1][1];
    double An = states[i - 1][2];
    double Bn = states[i - 1][3];
    
    // Compute mu (the forecast component)
    states[i][4] = level + (1.0 - 1.0 / theta) * 
                   (An * std::pow(1.0 - alpha, static_cast<double>(i)) +
                    Bn * (1.0 - std::pow(1.0 - alpha, static_cast<double>(i) + 1.0)) / alpha);
    
    // Use mu as observation if requested (for forecasting)
    if (usemu) {
        y = states[i][4];
    }
    
    // Update level (SES-style)
    states[i][0] = alpha * y + (1.0 - alpha) * level;
    
    // Update running mean
    states[i][1] = (static_cast<double>(i) * meany + y) / (static_cast<double>(i) + 1.0);
    
    // Update An, Bn
    if (model_type == ModelType::DSTM || model_type == ModelType::DOTM) {
        // Dynamic: update An and Bn at each step
        states[i][3] = ((static_cast<double>(i) - 1.0) * Bn + 
                        6.0 * (y - meany) / (static_cast<double>(i) + 1.0)) / 
                       (static_cast<double>(i) + 2.0);
        states[i][2] = states[i][1] - states[i][3] * (static_cast<double>(i) + 2.0) / 2.0;
    } else {
        // Static: keep An and Bn constant
        states[i][2] = An;
        states[i][3] = Bn;
    }
}

void forecast(const StateMatrix& states,
             size_t i,
             ModelType model_type,
             std::vector<double>& f,
             double alpha,
             double theta) {
    // Port of statsforecast theta_cpp.txt lines 63-74
    
    size_t h = f.size();
    StateMatrix new_states(i + h);
    
    // Copy existing states
    std::copy(states.begin(), states.begin() + i, new_states.begin());
    
    // Forecast future states
    for (size_t j = 0; j < h; ++j) {
        update(new_states, i + j, model_type, alpha, theta, 0.0, true);
        f[j] = new_states[i + j][4];
    }
}

double calc(const std::vector<double>& y,
           StateMatrix& states,
           ModelType model_type,
           double initial_smoothed,
           double alpha,
           double theta,
           std::vector<double>& e,
           std::vector<double>& amse,
           size_t nmse) {
    // Port of statsforecast theta_cpp.txt lines 76-107
    
    std::vector<double> denom(nmse, 0.0);
    std::vector<double> f(nmse);
    
    // Initialize state
    auto init_states = init_state(y, model_type, initial_smoothed, alpha, theta);
    states[0] = init_states;
    
    // Reset amse
    std::fill(amse.begin(), amse.end(), 0.0);
    
    // First error
    e[0] = y[0] - states[0][4];
    
    size_t n = y.size();
    
    // Iterate through time series
    for (size_t i = 1; i < n; ++i) {
        // Generate forecast
        forecast(states, i, model_type, f, alpha, theta);
        
        // Check for NA
        if (std::abs(f[0] - NA) < TOL) {
            return NA;
        }
        
        // Compute one-step-ahead error
        e[i] = y[i] - f[0];
        
        // Update AMSE for different horizons
        for (size_t j = 0; j < nmse; ++j) {
            if (i + j < n) {
                denom[j] += 1.0;
                double tmp = y[i + j] - f[j];
                amse[j] = (amse[j] * (denom[j] - 1.0) + tmp * tmp) / denom[j];
            }
        }
        
        // Update state with actual observation
        update(states, i, model_type, alpha, theta, y[i], false);
    }
    
    // Compute scaled MSE (excluding first 3 points)
    double mean_y = 0.0;
    for (double val : y) {
        mean_y += std::abs(val);
    }
    mean_y /= static_cast<double>(y.size());
    
    if (mean_y < TOL) {
        mean_y = TOL;
    }
    
    // Sum squared errors from index 3 onwards
    double sum_sq = 0.0;
    for (size_t i = 3; i < e.size(); ++i) {
        sum_sq += e[i] * e[i];
    }
    
    return sum_sq / mean_y;
}

// Objective function for Nelder-Mead
class ThetaObjective {
public:
    ThetaObjective(const std::vector<double>& y,
                   ModelType model_type,
                   bool opt_level,
                   bool opt_alpha,
                   bool opt_theta,
                   double init_level,
                   double init_alpha,
                   double init_theta,
                   size_t nmse)
        : y_(y), model_type_(model_type), opt_level_(opt_level),
          opt_alpha_(opt_alpha), opt_theta_(opt_theta),
          init_level_(init_level), init_alpha_(init_alpha), init_theta_(init_theta),
          nmse_(nmse) {}
    
    double operator()(const std::vector<double>& params) const {
        size_t j = 0;
        double level, alpha, theta;
        
        if (opt_level_) {
            level = params[j++];
        } else {
            level = init_level_;
        }
        
        if (opt_alpha_) {
            alpha = params[j++];
        } else {
            alpha = init_alpha_;
        }
        
        if (opt_theta_) {
            theta = params[j++];
        } else {
            theta = init_theta_;
        }
        
        // Bounds checking
        if (alpha <= 0.0 || alpha >= 1.0) return HUGE_N;
        if (theta <= 0.0) return HUGE_N;
        if (level <= 0.0) return HUGE_N;  // Assuming positive data
        
        StateMatrix states(y_.size());
        std::vector<double> e(y_.size());
        std::vector<double> amse(nmse_);
        
        double mse = calc(y_, states, model_type_, level, alpha, theta, e, amse, nmse_);
        
        // Handle special values
        mse = std::max(mse, -1e10);
        if (std::isnan(mse) || std::abs(mse + 99999) < 1e-7) {
            mse = HUGE_N;
        }
        
        return mse;
    }
    
private:
    const std::vector<double>& y_;
    ModelType model_type_;
    bool opt_level_;
    bool opt_alpha_;
    bool opt_theta_;
    double init_level_;
    double init_alpha_;
    double init_theta_;
    size_t nmse_;
};

OptimResult optimize(const std::vector<double>& y,
                    ModelType model_type,
                    bool opt_level,
                    bool opt_alpha,
                    bool opt_theta,
                    double init_level,
                    double init_alpha,
                    double init_theta,
                    size_t nmse,
                    OptimizerType optimizer_type) {
    // Build initial parameter vector
    std::vector<double> x0;
    std::vector<double> lower;
    std::vector<double> upper;
    
    if (opt_level) {
        x0.push_back(init_level);
        lower.push_back(0.1);  // Lower bound for level
        upper.push_back(y.back() * 10.0);  // Upper bound for level
    }
    
    if (opt_alpha) {
        x0.push_back(init_alpha);
        lower.push_back(0.01);  // Lower bound for alpha
        upper.push_back(0.99);  // Upper bound for alpha
    }
    
    if (opt_theta) {
        x0.push_back(init_theta);
        lower.push_back(1.0);   // Lower bound for theta
        upper.push_back(10.0);  // Upper bound for theta
    }
    
    if (x0.empty()) {
        // No optimization needed
        StateMatrix states(y.size());
        std::vector<double> e(y.size());
        std::vector<double> amse(nmse);
        double mse = calc(y, states, model_type, init_level, init_alpha, init_theta, e, amse, nmse);
        
        return {init_alpha, init_theta, init_level, mse, true, 0};
    }
    
    // Choose optimization method
    if (optimizer_type == OptimizerType::LBFGS) {
        // L-BFGS optimization with numerical gradients
        auto objective_fn = [&](const std::vector<double>& params, std::vector<double>& grad) -> double {
            size_t j = 0;
            double level = opt_level ? params[j++] : init_level;
            double alpha = opt_alpha ? params[j++] : init_alpha;
            double theta = opt_theta ? params[j++] : init_theta;
            
            // Bounds checking
            if (alpha <= 0.01 || alpha >= 0.99) {
                std::fill(grad.begin(), grad.end(), 0.0);
                return HUGE_N;
            }
            if (theta <= 1.0 || theta >= 10.0) {
                std::fill(grad.begin(), grad.end(), 0.0);
                return HUGE_N;
            }
            if (level <= 0.0) {
                std::fill(grad.begin(), grad.end(), 0.0);
                return HUGE_N;
            }
            
            // Compute MSE and gradients
            double mse = optimization::ThetaGradients::computeMSEWithGradients(
                y, model_type, level, alpha, theta,
                opt_level, opt_alpha, opt_theta, nmse, grad
            );
            
            return mse;
        };
        
        optimization::LBFGSOptimizer::Options lbfgs_options;
        lbfgs_options.max_iterations = 200;
        lbfgs_options.epsilon = 1e-6;
        lbfgs_options.m = 10;
        
        auto lbfgs_result = optimization::LBFGSOptimizer::minimize(
            objective_fn, x0, lower, upper, lbfgs_options
        );
        
        // Extract optimized parameters
        size_t j = 0;
        double opt_level_val = init_level;
        double opt_alpha_val = init_alpha;
        double opt_theta_val = init_theta;
        
        if (opt_level) {
            opt_level_val = lbfgs_result.x[j++];
        }
        if (opt_alpha) {
            opt_alpha_val = lbfgs_result.x[j++];
        }
        if (opt_theta) {
            opt_theta_val = lbfgs_result.x[j++];
        }
        
        return {opt_alpha_val, opt_theta_val, opt_level_val, lbfgs_result.fx,
                lbfgs_result.converged, lbfgs_result.iterations};
        
    } else {
        // Nelder-Mead optimization (fallback/legacy)
        ThetaObjective objective(y, model_type, opt_level, opt_alpha, opt_theta,
                                init_level, init_alpha, init_theta, nmse);
        
        utils::NelderMeadOptimizer optimizer;
        utils::NelderMeadOptimizer::Options options;
        options.step = 0.05;
        options.max_iterations = 1000;
        options.tolerance = 1e-4;
        options.alpha = 1.0;
        options.gamma = 2.0;
        options.rho = 0.5;
        options.sigma = 0.5;
        
        auto objective_fn = [&objective](const std::vector<double>& x) {
            return objective(x);
        };
        
        auto result = optimizer.minimize(objective_fn, x0, options, lower, upper);
        
        // Extract optimized parameters
        size_t j = 0;
        double opt_level_val = init_level;
        double opt_alpha_val = init_alpha;
        double opt_theta_val = init_theta;
        
        if (opt_level) {
            opt_level_val = result.best[j++];
        }
        if (opt_alpha) {
            opt_alpha_val = result.best[j++];
        }
        if (opt_theta) {
            opt_theta_val = result.best[j++];
        }
        
        return {opt_alpha_val, opt_theta_val, opt_level_val, result.value,
                result.converged, static_cast<int>(result.iterations)};
    }
}

} // namespace anofoxtime::models::theta_pegels

