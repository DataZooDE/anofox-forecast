#pragma once

#include <vector>
#include <array>
#include <cmath>

namespace anofoxtime::models::theta_pegels {

// Model types from statsforecast
enum class ModelType {
    STM,   // Standard Theta Method
    OTM,   // Optimized Theta Method
    DSTM,  // Dynamic Standard Theta Method
    DOTM   // Dynamic Optimized Theta Method
};

// Constants from statsforecast
constexpr double HUGE_N = 1e10;
constexpr double NA = -99999.0;
constexpr double TOL = 1e-10;

// State vector: [level, meany, An, Bn, mu]
using State = std::array<double, 5>;

// State matrix for all time steps
using StateMatrix = std::vector<State>;

/**
 * Initialize state vector from first observation
 * Port of statsforecast's init_state() function
 */
State init_state(const std::vector<double>& y,
                 ModelType model_type,
                 double initial_smoothed,
                 double alpha,
                 double theta);

/**
 * Update state at time step i
 * Port of statsforecast's update() function
 */
void update(StateMatrix& states,
           size_t i,
           ModelType model_type,
           double alpha,
           double theta,
           double y,
           bool usemu);

/**
 * Generate forecast from current state
 * Port of statsforecast's forecast() function
 * 
 * @param workspace Pre-allocated workspace for state calculations (must be at least i+h size)
 */
void forecast(const StateMatrix& states,
             size_t i,
             ModelType model_type,
             std::vector<double>& f,
             double alpha,
             double theta,
             StateMatrix& workspace);

/**
 * Calculate MSE and residuals over training data
 * Port of statsforecast's calc() function
 */
double calc(const std::vector<double>& y,
           StateMatrix& states,
           ModelType model_type,
           double initial_smoothed,
           double alpha,
           double theta,
           std::vector<double>& e,
           std::vector<double>& amse,
           size_t nmse);

/**
 * Optimization result
 */
struct OptimResult {
    double alpha;
    double theta;
    double level;
    double mse;
    bool converged;
    int iterations;
};

/**
 * Optimization method selection
 */
enum class OptimizerType {
    NelderMead,  // Derivative-free, robust but slower
    LBFGS        // Gradient-based, faster for smooth problems
};

/**
 * Optimize alpha, theta, and initial level
 * 
 * @param optimizer Which optimization method to use (default: LBFGS for better performance)
 */
OptimResult optimize(const std::vector<double>& y,
                    ModelType model_type,
                    bool opt_level,
                    bool opt_alpha,
                    bool opt_theta,
                    double init_level,
                    double init_alpha,
                    double init_theta,
                    size_t nmse = 3,
                    OptimizerType optimizer = OptimizerType::LBFGS);

} // namespace anofoxtime::models::theta_pegels

