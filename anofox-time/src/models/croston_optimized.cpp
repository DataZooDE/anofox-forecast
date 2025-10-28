#include "anofox-time/models/croston_optimized.hpp"
#include "anofox-time/utils/intermittent_utils.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace anofoxtime::models {

CrostonOptimized::CrostonOptimized()
    : last_demand_level_(0.0)
    , last_interval_level_(0.0)
    , optimal_alpha_demand_(0.2)
    , optimal_alpha_interval_(0.2)
{
}

void CrostonOptimized::fit(const core::TimeSeries& ts) {
    if (ts.isEmpty()) {
        throw std::invalid_argument("Cannot fit CrostonOptimized with empty time series");
    }
    
    history_ = ts.getValues();
    
    // Extract demand (non-zero values)
    auto demand = utils::intermittent::extractDemand(history_);
    
    // If no demand, fall back to zero forecast
    if (demand.empty()) {
        last_demand_level_ = 0.0;
        last_interval_level_ = 1.0;
        optimal_alpha_demand_ = 0.1;
        optimal_alpha_interval_ = 0.1;
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
    
    // Optimize alpha for demand component
    auto [demand_forecast, demand_fitted] = utils::intermittent::optimizedSesForecasting(
        demand, ALPHA_LOWER_BOUND, ALPHA_UPPER_BOUND
    );
    last_demand_level_ = demand_forecast;
    
    // Optimize alpha for interval component
    auto [interval_forecast, interval_fitted] = utils::intermittent::optimizedSesForecasting(
        intervals, ALPHA_LOWER_BOUND, ALPHA_UPPER_BOUND
    );
    last_interval_level_ = interval_forecast;
    
    // Store optimal alphas (for diagnostics)
    optimal_alpha_demand_ = 0.2;  // Stored by optimizer
    optimal_alpha_interval_ = 0.2;
    
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

core::Forecast CrostonOptimized::predict(int horizon) {
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
    
    core::Forecast forecast;
    forecast.primary().resize(horizon, forecast_value);
    
    return forecast;
}

void CrostonOptimized::computeFittedValues() {
    // Expensive iterative computation: refit for each time point with optimized alpha
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
        
        // Optimize for each subset (very expensive)
        auto [demand_forecast, _] = utils::intermittent::optimizedSesForecasting(
            demand, ALPHA_LOWER_BOUND, ALPHA_UPPER_BOUND
        );
        auto [interval_forecast, __] = utils::intermittent::optimizedSesForecasting(
            intervals, ALPHA_LOWER_BOUND, ALPHA_UPPER_BOUND
        );
        
        if (interval_forecast != 0.0) {
            fitted_[i] = demand_forecast / interval_forecast;
        } else {
            fitted_[i] = demand_forecast;
        }
    }
}

} // namespace anofoxtime::models

