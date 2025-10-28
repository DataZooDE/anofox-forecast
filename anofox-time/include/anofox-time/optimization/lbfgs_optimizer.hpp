#pragma once

#include <vector>
#include <functional>
#include <limits>
#include <cmath>
#include <string>

namespace anofoxtime::optimization {

/**
 * @brief L-BFGS optimizer for bounded optimization problems
 * 
 * Wrapper around LBFGS++ library for parameter optimization in ETS models.
 * Supports box constraints for parameters like alpha, gamma, etc.
 */
class LBFGSOptimizer {
public:
    struct Result {
        std::vector<double> x;           // Optimal parameters
        double fx;                       // Final objective value
        int iterations;                  // Number of iterations
        bool converged;                  // Whether optimization converged
        std::string message;             // Status message
    };
    
    struct Options {
        int max_iterations;
        double epsilon;           // Convergence tolerance
        int m;                    // Number of corrections (L-BFGS memory)
        double ftol;              // Function tolerance
        double gtol;              // Gradient tolerance
        bool verbose;
        
        Options() 
            : max_iterations(200), epsilon(1e-6), m(10), 
              ftol(1e-6), gtol(1e-5), verbose(false) {}
    };
    
    /**
     * @brief Minimize objective function with box constraints
     * 
     * @param objective Function that computes f(x) and gradient g(x)
     * @param x0 Initial parameters
     * @param lower Lower bounds for each parameter
     * @param upper Upper bounds for each parameter
     * @param options Optimization options
     * @return Result containing optimal parameters and diagnostics
     */
    static Result minimize(
        std::function<double(const std::vector<double>&, std::vector<double>&)> objective,
        const std::vector<double>& x0,
        const std::vector<double>& lower,
        const std::vector<double>& upper,
        const Options& options = Options()
    );
    
private:
    // Project parameters onto feasible region [lower, upper]
    static void projectBounds(std::vector<double>& x, 
                             const std::vector<double>& lower,
                             const std::vector<double>& upper);
    
    // Check if gradient indicates we're at a bound
    static bool atBoundary(const std::vector<double>& x,
                          const std::vector<double>& g,
                          const std::vector<double>& lower,
                          const std::vector<double>& upper);
};

} // namespace anofoxtime::optimization

