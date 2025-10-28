#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Optimized Croston's method with automatic alpha selection
 * 
 * Similar to CrostonClassic but optimizes the smoothing parameters
 * for both demand and interval components using Nelder-Mead optimization.
 * 
 * Alpha bounds: [0.1, 0.3] as per statsforecast implementation
 * 
 * Formula: ŷ_t = ẑ_t / p̂_t
 * where ẑ_t and p̂_t use optimized alpha values
 */
class CrostonOptimized : public IForecaster {
public:
    CrostonOptimized();
    
    void fit(const core::TimeSeries& ts) override;
    core::Forecast predict(int horizon) override;
    
    std::string getName() const override {
        return "CrostonOptimized";
    }
    
    // Accessors
    const std::vector<double>& fittedValues() const {
        return fitted_;
    }
    
    const std::vector<double>& residuals() const {
        return residuals_;
    }
    
    double lastDemandLevel() const {
        return last_demand_level_;
    }
    
    double lastIntervalLevel() const {
        return last_interval_level_;
    }
    
    double optimalAlphaDemand() const {
        return optimal_alpha_demand_;
    }
    
    double optimalAlphaInterval() const {
        return optimal_alpha_interval_;
    }
    
private:
    static constexpr double ALPHA_LOWER_BOUND = 0.1;
    static constexpr double ALPHA_UPPER_BOUND = 0.3;
    
    double last_demand_level_;
    double last_interval_level_;
    double optimal_alpha_demand_;
    double optimal_alpha_interval_;
    std::vector<double> history_;
    std::vector<double> fitted_;
    std::vector<double> residuals_;
    bool is_fitted_ = false;
    
    void computeFittedValues();
};

} // namespace anofoxtime::models


