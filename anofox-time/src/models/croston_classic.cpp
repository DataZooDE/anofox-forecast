#include "anofox-time/models/croston_classic.hpp"
#include "anofox-time/utils/intermittent_utils.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace anofoxtime::models {

CrostonClassic::CrostonClassic()
    : last_demand_level_(0.0)
    , last_interval_level_(0.0)
{
}

void CrostonClassic::fit(const core::TimeSeries& ts) {
    if (ts.isEmpty()) {
        throw std::invalid_argument("Cannot fit CrostonClassic with empty time series");
    }
    
    history_ = ts.getValues();
    
    // Extract demand (non-zero values)
    auto demand = utils::intermittent::extractDemand(history_);
    
    // If no demand, fall back to zero forecast
    if (demand.empty()) {
        last_demand_level_ = 0.0;
        last_interval_level_ = 1.0;
        is_fitted_ = true;
        
        // Fitted values are all zeros
        fitted_ = std::vector<double>(history_.size(), 0.0);
        fitted_[0] = std::numeric_limits<double>::quiet_NaN();
        
        residuals_ = history_;
        residuals_[0] = std::numeric_limits<double>::quiet_NaN();
        return;
    }
    
    // Compute intervals between non-zero elements
    auto intervals = utils::intermittent::computeIntervals(history_);
    
    // Apply SES to demand with alpha = 0.1
    auto [demand_forecast, demand_fitted] = utils::intermittent::sesForecasting(demand, ALPHA);
    last_demand_level_ = demand_forecast;
    
    // Apply SES to intervals with alpha = 0.1
    auto [interval_forecast, interval_fitted] = utils::intermittent::sesForecasting(intervals, ALPHA);
    last_interval_level_ = interval_forecast;
    
    // Compute fitted values (expensive iterative approach)
    computeFittedValues();
    
    // Compute residuals
    residuals_.resize(history_.size());
    residuals_[0] = std::numeric_limits<double>::quiet_NaN();
    for (size_t i = 1; i < history_.size(); ++i) {
        residuals_[i] = history_[i] - fitted_[i];
    }
    
    is_fitted_ = true;
}

core::Forecast CrostonClassic::predict(int horizon) {
    if (!is_fitted_) {
        throw std::runtime_error("Model must be fitted before prediction");
    }
    
    if (horizon <= 0) {
        throw std::invalid_argument("Horizon must be positive");
    }
    
    // Forecast is constant for all horizons
    double forecast_value;
    if (last_interval_level_ != 0.0) {
        forecast_value = last_demand_level_ / last_interval_level_;
    } else {
        forecast_value = last_demand_level_;
    }
    
    // Apply bias factor (overridden in CrostonSBA)
    forecast_value = applyBiasFactor(forecast_value);
    
    core::Forecast forecast;
    forecast.primary().resize(horizon, forecast_value);
    
    return forecast;
}

void CrostonClassic::computeFittedValues() {
    // Expensive iterative computation: refit for each time point
    fitted_.resize(history_.size());
    fitted_[0] = std::numeric_limits<double>::quiet_NaN();
    
    for (size_t i = 1; i < history_.size(); ++i) {
        // Use data up to time i-1 to predict time i
        std::vector<double> partial_history(history_.begin(), history_.begin() + i);
        
        auto demand = utils::intermittent::extractDemand(partial_history);
        auto intervals = utils::intermittent::computeIntervals(partial_history);
        
        if (demand.empty() || intervals.empty()) {
            fitted_[i] = 0.0;
            continue;
        }
        
        auto [demand_forecast, _] = utils::intermittent::sesForecasting(demand, ALPHA);
        auto [interval_forecast, __] = utils::intermittent::sesForecasting(intervals, ALPHA);
        
        if (interval_forecast != 0.0) {
            fitted_[i] = applyBiasFactor(demand_forecast / interval_forecast);
        } else {
            fitted_[i] = applyBiasFactor(demand_forecast);
        }
    }
}

} // namespace anofoxtime::models

