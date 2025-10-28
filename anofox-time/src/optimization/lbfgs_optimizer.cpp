#include "anofox-time/optimization/lbfgs_optimizer.hpp"
#include <LBFGSB.h>
#include <algorithm>
#include <iostream>

namespace anofoxtime::optimization {

using namespace LBFGSpp;

void LBFGSOptimizer::projectBounds(std::vector<double>& x, 
                                   const std::vector<double>& lower,
                                   const std::vector<double>& upper) {
    for (size_t i = 0; i < x.size(); ++i) {
        x[i] = std::max(lower[i], std::min(x[i], upper[i]));
    }
}

bool LBFGSOptimizer::atBoundary(const std::vector<double>& x,
                                const std::vector<double>& g,
                                const std::vector<double>& lower,
                                const std::vector<double>& upper) {
    constexpr double boundary_tol = 1e-6;
    constexpr double gradient_tol = 1e-3;
    
    for (size_t i = 0; i < x.size(); ++i) {
        bool at_lower = std::abs(x[i] - lower[i]) < boundary_tol;
        bool at_upper = std::abs(x[i] - upper[i]) < boundary_tol;
        bool gradient_positive = g[i] > gradient_tol;
        bool gradient_negative = g[i] < -gradient_tol;
        
        // At lower bound with negative gradient, or upper bound with positive gradient
        if ((at_lower && gradient_negative) || (at_upper && gradient_positive)) {
            return true;
        }
    }
    return false;
}

LBFGSOptimizer::Result LBFGSOptimizer::minimize(
    std::function<double(const std::vector<double>&, std::vector<double>&)> objective,
    const std::vector<double>& x0,
    const std::vector<double>& lower,
    const std::vector<double>& upper,
    const Options& options) {
    
    Result result;
    result.converged = false;
    result.iterations = 0;
    
    const int n = static_cast<int>(x0.size());
    
    // Convert to Eigen types
    Eigen::VectorXd x = Eigen::VectorXd::Map(x0.data(), n);
    Eigen::VectorXd lb = Eigen::VectorXd::Map(lower.data(), n);
    Eigen::VectorXd ub = Eigen::VectorXd::Map(upper.data(), n);
    
    // Project initial point onto feasible region
    x = x.cwiseMax(lb).cwiseMin(ub);
    
    // Set up L-BFGS-B parameters
    LBFGSBParam<double> param;
    param.max_iterations = options.max_iterations;
    param.epsilon = options.epsilon;
    param.epsilon_rel = options.epsilon;
    param.m = options.m;
    param.ftol = options.ftol;
    param.wolfe = 0.9;
    param.max_linesearch = 20;
    
    // Create solver
    LBFGSBSolver<double> solver(param);
    
    // Wrap objective function for Eigen
    auto eigen_objective = [&](const Eigen::VectorXd& x_eigen, Eigen::VectorXd& grad_eigen) {
        // Convert to std::vector for user function
        std::vector<double> x_vec(n);
        std::vector<double> grad_vec(n);
        
        for (int i = 0; i < n; ++i) {
            x_vec[i] = x_eigen[i];
        }
        
        // Evaluate objective and gradient
        double fx = objective(x_vec, grad_vec);
        
        // Convert gradient back to Eigen
        for (int i = 0; i < n; ++i) {
            grad_eigen[i] = grad_vec[i];
        }
        
        return fx;
    };
    
    // Run optimization
    double fx;
    try {
        result.iterations = solver.minimize(eigen_objective, x, fx, lb, ub);
        result.fx = fx;
        result.converged = true;
        result.message = "Converged";
        
        if (options.verbose) {
            std::cerr << "[LBFGS] Converged in " << result.iterations 
                     << " iterations, f = " << fx << std::endl;
        }
    } catch (const std::exception& e) {
        result.converged = false;
        result.message = std::string("Failed: ") + e.what();
        
        if (options.verbose) {
            std::cerr << "[LBFGS] Failed: " << e.what() << std::endl;
        }
    }
    
    // Extract result
    result.x.resize(n);
    for (int i = 0; i < n; ++i) {
        result.x[i] = x[i];
    }
    
    // Project final result onto bounds (ensure feasibility)
    projectBounds(result.x, lower, upper);
    
    return result;
}

} // namespace anofoxtime::optimization

