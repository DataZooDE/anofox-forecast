#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Teunter-Syntetos-Babai (TSB) method for intermittent demand
 * 
 * A modification of Croston's method that replaces inter-demand intervals
 * with demand probability d_t (1 if demand occurs, 0 otherwise).
 * 
 * Formula: ŷ_t = d̂_t * ẑ_t
 * 
 * where:
 * - ẑ_t: SES forecast of non-zero demand sizes (with alpha_d)
 * - d̂_t: SES forecast of demand probability (with alpha_p)
 * 
 * Reference: Teunter, R. H., Syntetos, A. A., & Babai, M. Z. (2011). 
 * Intermittent demand: Linking forecasting to inventory obsolescence.
 */
class TSB : public IForecaster {
public:
    /**
     * @brief Constructor with smoothing parameters
     * @param alpha_d Smoothing parameter for demand component
     * @param alpha_p Smoothing parameter for probability component
     */
    TSB(double alpha_d, double alpha_p);
    
    void fit(const core::TimeSeries& ts) override;
    core::Forecast predict(int horizon) override;
    
    std::string getName() const override {
        return "TSB";
    }
    
    // Accessors
    const std::vector<double>& fittedValues() const {
        return fitted_;
    }
    
    const std::vector<double>& residuals() const {
        return residuals_;
    }
    
    double alphaDemand() const {
        return alpha_d_;
    }
    
    double alphaProbability() const {
        return alpha_p_;
    }
    
    double lastDemandLevel() const {
        return last_demand_level_;
    }
    
    double lastProbabilityLevel() const {
        return last_probability_level_;
    }
    
private:
    double alpha_d_;  // Smoothing parameter for demand
    double alpha_p_;  // Smoothing parameter for probability
    double last_demand_level_;
    double last_probability_level_;
    std::vector<double> history_;
    std::vector<double> fitted_;
    std::vector<double> residuals_;
    bool is_fitted_ = false;
    
    void computeFittedValues();
};

} // namespace anofoxtime::models


