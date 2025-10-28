#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Croston's Classic method for intermittent demand forecasting
 * 
 * Decomposes intermittent time series into:
 * - Non-zero demand sizes (z_t)
 * - Inter-demand intervals (p_t)
 * 
 * Forecast: ŷ_t = ẑ_t / p̂_t
 * 
 * Both components forecasted using SES with fixed alpha = 0.1
 * 
 * Reference: Croston, J. D. (1972). Forecasting and stock control for 
 * intermittent demands. Journal of the Operational Research Society, 23(3), 289-303.
 */
class CrostonClassic : public IForecaster {
public:
    CrostonClassic();
    
    void fit(const core::TimeSeries& ts) override;
    core::Forecast predict(int horizon) override;
    
    std::string getName() const override {
        return "CrostonClassic";
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
    
protected:
    static constexpr double ALPHA = 0.1;  // Fixed smoothing parameter
    
    double last_demand_level_;
    double last_interval_level_;
    std::vector<double> history_;
    std::vector<double> fitted_;
    std::vector<double> residuals_;
    bool is_fitted_ = false;
    
    void computeFittedValues();
    
    // Protected method for CrostonSBA to reuse
    virtual double applyBiasFactor(double forecast) const {
        return forecast;
    }
};

} // namespace anofoxtime::models


