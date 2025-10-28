#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/theta_pegels.hpp"
#include <memory>

namespace anofoxtime::models {

/**
 * Dynamic Theta Method (DSTM)
 * Uses Pegels state-space with dynamic An/Bn updates
 */
class DynamicTheta : public IForecaster {
public:
    explicit DynamicTheta(int seasonal_period = 1, double theta_param = 2.0);
    
    void fit(const core::TimeSeries& ts) override;
    void fitRaw(const std::vector<double>& data);
    core::Forecast predict(int horizon) override;
    core::Forecast predictWithConfidence(int horizon, double confidence);
    
    std::string getName() const override {
        return "DynamicTheta";
    }
    
    // Accessors
    const std::vector<double>& fittedValues() const { return fitted_; }
    const std::vector<double>& residuals() const { return residuals_; }
    double getAlpha() const { return alpha_; }
    double getTheta() const { return theta_; }
    
    // Setters
    void setAlpha(double alpha) { alpha_ = alpha; }
    
    // Public helper for subclasses
    std::vector<double> deseasonalize(const std::vector<double>& data);
    
private:
    int seasonal_period_;
    double theta_;
    double alpha_;
    
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

