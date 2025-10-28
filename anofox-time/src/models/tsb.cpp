#include "anofox-time/models/tsb.hpp"
#include "anofox-time/utils/intermittent_utils.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace anofoxtime::models {

TSB::TSB(double alpha_d, double alpha_p)
    : alpha_d_(alpha_d)
    , alpha_p_(alpha_p)
    , last_demand_level_(0.0)
    , last_probability_level_(0.0)
{
    if (alpha_d < 0.0 || alpha_d > 1.0) {
        throw std::invalid_argument("alpha_d must be between 0 and 1");
    }
    if (alpha_p < 0.0 || alpha_p > 1.0) {
        throw std::invalid_argument("alpha_p must be between 0 and 1");
    }
}

void TSB::fit(const core::TimeSeries& ts) {
    if (ts.isEmpty()) {
        throw std::invalid_argument("Cannot fit TSB with empty time series");
    }
    
    history_ = ts.getValues();
    
    // Check if all zeros
    bool all_zeros = true;
    for (double val : history_) {
        if (val != 0.0) {
            all_zeros = false;
            break;
        }
    }
    
    if (all_zeros) {
        last_demand_level_ = 0.0;
        last_probability_level_ = 0.0;
        is_fitted_ = true;
        
        fitted_ = std::vector<double>(history_.size(), 0.0);
        fitted_[0] = std::numeric_limits<double>::quiet_NaN();
        
        residuals_ = std::vector<double>(history_.size(), 0.0);
        residuals_[0] = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    
    // Extract demand (non-zero values)
    auto demand = utils::intermittent::extractDemand(history_);
    
    // Compute probability (0/1 indicator)
    auto probability = utils::intermittent::computeProbability(history_);
    
    // Apply SES to demand with alpha_d
    auto [demand_forecast, demand_fitted] = utils::intermittent::sesForecasting(demand, alpha_d_);
    last_demand_level_ = demand_forecast;
    
    // Apply SES to probability with alpha_p
    auto [probability_forecast, probability_fitted] = utils::intermittent::sesForecasting(probability, alpha_p_);
    last_probability_level_ = probability_forecast;
    
    // Expand demand fitted values to original series length
    auto expanded_demand = utils::intermittent::expandFittedDemand(demand_fitted, history_);
    
    // Compute fitted values: probability_fitted * expanded_demand
    fitted_.resize(history_.size());
    fitted_[0] = std::numeric_limits<double>::quiet_NaN();
    for (size_t i = 1; i < history_.size(); ++i) {
        fitted_[i] = probability_fitted[i] * expanded_demand[i];
    }
    
    // Compute residuals
    residuals_.resize(history_.size());
    residuals_[0] = std::numeric_limits<double>::quiet_NaN();
    for (size_t i = 1; i < history_.size(); ++i) {
        residuals_[i] = history_[i] - fitted_[i];
    }
    
    is_fitted_ = true;
}

core::Forecast TSB::predict(int horizon) {
    if (!is_fitted_) {
        throw std::runtime_error("Model must be fitted before prediction");
    }
    
    if (horizon <= 0) {
        throw std::invalid_argument("Horizon must be positive");
    }
    
    // Forecast: probability * demand
    double forecast_value = last_probability_level_ * last_demand_level_;
    
    core::Forecast forecast;
    forecast.primary().resize(horizon, forecast_value);
    
    return forecast;
}

void TSB::computeFittedValues() {
    // This method is not needed as fit() computes fitted values directly
}

} // namespace anofoxtime::models

