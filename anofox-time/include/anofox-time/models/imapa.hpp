#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Intermittent Multiple Aggregation Prediction Algorithm (IMAPA)
 * 
 * Similar to ADIDA but considers multiple aggregation levels to capture
 * different dynamics. Tests all levels from 1 to max_aggregation_level
 * (based on mean interval), applies optimized SES at each level, and
 * averages the forecasts.
 * 
 * Steps:
 * 1. Compute mean inter-demand interval
 * 2. For each level k = 1 to round(mean_interval):
 *    a. Aggregate data at level k
 *    b. Apply optimized SES
 *    c. Disaggregate: forecast_k = ses_forecast / k
 * 3. Final forecast = mean(all forecast_k)
 * 
 * Reference: Syntetos, A. A., & Boylan, J. E. (2021). Intermittent demand
 * forecasting: Context, methods and applications.
 */
class IMAPA : public IForecaster {
public:
    IMAPA();
    
    void fit(const core::TimeSeries& ts) override;
    core::Forecast predict(int horizon) override;
    
    std::string getName() const override {
        return "IMAPA";
    }
    
    // Accessors
    const std::vector<double>& fittedValues() const {
        return fitted_;
    }
    
    const std::vector<double>& residuals() const {
        return residuals_;
    }
    
    int maxAggregationLevel() const {
        return max_aggregation_level_;
    }
    
    double forecast() const {
        return forecast_value_;
    }
    
private:
    int max_aggregation_level_;
    double forecast_value_;
    std::vector<double> history_;
    std::vector<double> fitted_;
    std::vector<double> residuals_;
    bool is_fitted_ = false;
    
    void computeFittedValues();
};

} // namespace anofoxtime::models


