#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Aggregate-Disaggregate Intermittent Demand Approach (ADIDA)
 * 
 * Uses temporal aggregation to reduce zero observations, applies optimized
 * SES at the aggregated level, then disaggregates using equal weights.
 * 
 * Steps:
 * 1. Compute mean inter-demand interval
 * 2. Aggregate data at level = round(mean_interval)
 * 3. Apply optimized SES to aggregated series
 * 4. Disaggregate: forecast_original = forecast_agg / aggregation_level
 * 
 * Specializes in sparse/intermittent series with very few non-zero observations.
 * 
 * Reference: Nikolopoulos, K., et al. (2011). An aggregate–disaggregate 
 * intermittent demand approach (ADIDA) to forecasting.
 */
class ADIDA : public IForecaster {
public:
    ADIDA();
    
    void fit(const core::TimeSeries& ts) override;
    core::Forecast predict(int horizon) override;
    
    std::string getName() const override {
        return "ADIDA";
    }
    
    // Accessors
    const std::vector<double>& fittedValues() const {
        return fitted_;
    }
    
    const std::vector<double>& residuals() const {
        return residuals_;
    }
    
    int aggregationLevel() const {
        return aggregation_level_;
    }
    
    double forecast() const {
        return forecast_value_;
    }
    
private:
    int aggregation_level_;
    double forecast_value_;
    std::vector<double> history_;
    std::vector<double> fitted_;
    std::vector<double> residuals_;
    bool is_fitted_ = false;
    
    void computeFittedValues();
};

} // namespace anofoxtime::models


