#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/theta_pegels.hpp"
#include <memory>

namespace anofoxtime::models {

/**
 * Theta Method using Pegels state-space formulation
 * Standard Theta Method (STM) - fixed theta=2.0
 */
class Theta : public IForecaster {
public:
    explicit Theta(int seasonal_period = 1, double theta_param = 2.0);
    
    void fit(const core::TimeSeries& ts) override;
    void fitRaw(const std::vector<double>& data);
    core::Forecast predict(int horizon) override;
    core::Forecast predictWithConfidence(int horizon, double confidence);
    
    std::string getName() const override {
        return "Theta";
    }
    
    // Accessors
    const std::vector<double>& fittedValues() const { return fitted_; }
    const std::vector<double>& residuals() const { return residuals_; }
    double getAlpha() const { return alpha_; }
    double getTheta() const { return theta_; }
    double getLevel() const { return level_; }
    
    // Setters
    void setAlpha(double alpha) { alpha_ = alpha; }
    
    // Public helper for subclasses
    std::vector<double> deseasonalize(const std::vector<double>& data);
    
private:
    int seasonal_period_;
    double theta_;
    double alpha_;
    double level_;
    
    std::vector<double> history_;
    std::vector<double> deseasonalized_;
    std::vector<double> seasonal_indices_;
    std::vector<double> fitted_;
    std::vector<double> residuals_;
    
    theta_pegels::StateMatrix states_;
    
    bool is_fitted_ = false;
    
    // Helper methods
    std::vector<double> reseasonalize(const std::vector<double>& forecast) const;
    void computeFittedValues();
};

} // namespace anofoxtime::models

