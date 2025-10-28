#include "anofox-time/models/adida.hpp"
#include "anofox-time/utils/intermittent_utils.hpp"
#include <cmath>
#include <numeric>
#include <limits>
#include <stdexcept>

namespace anofoxtime::models {

ADIDA::ADIDA()
    : aggregation_level_(1)
    , forecast_value_(0.0)
{
}

void ADIDA::fit(const core::TimeSeries& ts) {
    if (ts.isEmpty()) {
        throw std::invalid_argument("Cannot fit ADIDA with empty time series");
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
        aggregation_level_ = 1;
        forecast_value_ = 0.0;
        fitted_ = std::vector<double>(history_.size(), 0.0);
        fitted_[0] = std::numeric_limits<double>::quiet_NaN();
        residuals_ = std::vector<double>(history_.size(), 0.0);
        residuals_[0] = std::numeric_limits<double>::quiet_NaN();
        is_fitted_ = true;
        return;
    }
    
    // Compute intervals between non-zero elements
    auto intervals = utils::intermittent::computeIntervals(history_);
    
    if (intervals.empty()) {
        aggregation_level_ = 1;
        forecast_value_ = history_.back();
        fitted_ = history_;
        fitted_[0] = std::numeric_limits<double>::quiet_NaN();
        residuals_ = std::vector<double>(history_.size(), 0.0);
        residuals_[0] = std::numeric_limits<double>::quiet_NaN();
        is_fitted_ = true;
        return;
    }
    
    // Compute mean interval and aggregation level
    double mean_interval = std::accumulate(intervals.begin(), intervals.end(), 0.0) / intervals.size();
    aggregation_level_ = std::max(1, static_cast<int>(std::round(mean_interval)));
    
    // Compute forecast at aggregation level
    double sums_forecast = utils::intermittent::chunkForecast(history_, aggregation_level_);
    forecast_value_ = sums_forecast / aggregation_level_;
    
    // Compute fitted values (expensive: recompute for each expanding window)
    computeFittedValues();
    
    // Compute residuals
    residuals_.resize(history_.size());
    residuals_[0] = std::numeric_limits<double>::quiet_NaN();
    for (size_t i = 1; i < history_.size(); ++i) {
        residuals_[i] = history_[i] - fitted_[i];
    }
    
    is_fitted_ = true;
}

core::Forecast ADIDA::predict(int horizon) {
    if (!is_fitted_) {
        throw std::runtime_error("Model must be fitted before prediction");
    }
    
    if (horizon <= 0) {
        throw std::invalid_argument("Horizon must be positive");
    }
    
    core::Forecast forecast;
    forecast.primary().resize(horizon, forecast_value_);
    
    return forecast;
}

void ADIDA::computeFittedValues() {
    // Expensive iterative computation: recompute aggregation level for each expanding window
    fitted_.resize(history_.size());
    fitted_[0] = std::numeric_limits<double>::quiet_NaN();
    
    for (size_t i = 1; i < history_.size(); ++i) {
        // Use data up to time i to predict time i
        std::vector<double> partial_history(history_.begin(), history_.begin() + i);
        
        // Compute intervals for this subset
        auto intervals = utils::intermittent::computeIntervals(partial_history);
        
        if (intervals.empty()) {
            fitted_[i] = 0.0;
            continue;
        }
        
        // Compute cumulative mean interval up to this point
        double cumsum = 0.0;
        for (size_t j = 0; j <= std::min(intervals.size() - 1, i - 1); ++j) {
            cumsum += intervals[j];
        }
        double mean_interval_i = cumsum / std::min(intervals.size(), i);
        
        int agg_level_i = std::max(1, static_cast<int>(std::round(mean_interval_i)));
        
        // Compute forecast at this aggregation level
        double sums_forecast = utils::intermittent::chunkForecast(partial_history, agg_level_i);
        fitted_[i] = sums_forecast / agg_level_i;
    }
}

} // namespace anofoxtime::models

