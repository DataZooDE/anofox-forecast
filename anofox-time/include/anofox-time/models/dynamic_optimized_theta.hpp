#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/theta_pegels.hpp"
#include "anofox-time/models/dynamic_theta.hpp"
#include <memory>

namespace anofoxtime::models {

/**
 * Dynamic Optimized Theta Method (DOTM)
 * Uses Nelder-Mead to optimize alpha and theta with dynamic An/Bn
 */
class DynamicOptimizedTheta : public IForecaster {
public:
    explicit DynamicOptimizedTheta(int seasonal_period = 1,
                                  theta_pegels::OptimizerType optimizer = theta_pegels::OptimizerType::NelderMead);
    
    void fit(const core::TimeSeries& ts) override;
    core::Forecast predict(int horizon) override;
    core::Forecast predictWithConfidence(int horizon, double confidence);
    
    std::string getName() const override {
        return "DynamicOptimizedTheta";
    }
    
    // Accessors
    const std::vector<double>& fittedValues() const;
    const std::vector<double>& residuals() const;
    double getOptimalAlpha() const { return optimal_alpha_; }
    double getOptimalTheta() const { return optimal_theta_; }
    double getOptimalLevel() const { return optimal_level_; }
    
private:
    int seasonal_period_;
    theta_pegels::OptimizerType optimizer_;
    double optimal_alpha_;
    double optimal_theta_;
    double optimal_level_;
    double optimal_mse_;
    
    std::unique_ptr<DynamicTheta> fitted_model_;
    bool is_fitted_ = false;
};

} // namespace anofoxtime::models

