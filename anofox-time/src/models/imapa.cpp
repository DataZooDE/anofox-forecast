#include "anofox-time/models/imapa.hpp"
#include "anofox-time/utils/intermittent_utils.hpp"
#include <cmath>
#include <numeric>
#include <limits>
#include <stdexcept>

namespace anofoxtime::models {

IMAPA::IMAPA()
    : max_aggregation_level_(1)
    , forecast_value_(0.0)
{
}

void IMAPA::fit(const core::TimeSeries& ts) {
    if (ts.isEmpty()) {
        throw std::invalid_argument("Cannot fit IMAPA with empty time series");
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
        max_aggregation_level_ = 1;
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
        max_aggregation_level_ = 1;
        forecast_value_ = history_.back();
        fitted_ = history_;
        fitted_[0] = std::numeric_limits<double>::quiet_NaN();
        residuals_ = std::vector<double>(history_.size(), 0.0);
        residuals_[0] = std::numeric_limits<double>::quiet_NaN();
        is_fitted_ = true;
        return;
    }
    
    // Compute mean interval
    double mean_interval = std::accumulate(intervals.begin(), intervals.end(), 0.0) / intervals.size();
    max_aggregation_level_ = std::max(1, static_cast<int>(std::round(mean_interval)));
    
    // Compute forecasts at each aggregation level and average
    std::vector<double> forecasts;
    forecasts.reserve(max_aggregation_level_);
    
    for (int aggregation_level = 1; aggregation_level <= max_aggregation_level_; ++aggregation_level) {
        // Discard remainder to get complete chunks
        int lost_remainder = static_cast<int>(history_.size()) % aggregation_level;
        std::vector<double> y_cut;
        
        if (lost_remainder > 0 && history_.size() > static_cast<size_t>(lost_remainder)) {
            y_cut.assign(history_.begin() + lost_remainder, history_.end());
        } else if (lost_remainder == 0) {
            y_cut = history_;
        } else {
            // Not enough data for this aggregation level
            continue;
        }
        
        // Aggregate into chunks
        auto aggregation_sums = utils::intermittent::chunkSums(y_cut, aggregation_level);
        
        if (aggregation_sums.empty()) {
            continue;
        }
        
        // Apply optimized SES
        auto [forecast_agg, _] = utils::intermittent::optimizedSesForecasting(aggregation_sums);
        
        // Disaggregate
        double forecast_level = forecast_agg / aggregation_level;
        forecasts.push_back(forecast_level);
    }
    
    // Average all forecasts
    if (!forecasts.empty()) {
        forecast_value_ = std::accumulate(forecasts.begin(), forecasts.end(), 0.0) / forecasts.size();
    } else {
        forecast_value_ = 0.0;
    }
    
    // Compute fitted values (very expensive)
    computeFittedValues();
    
    // Compute residuals
    residuals_.resize(history_.size());
    residuals_[0] = std::numeric_limits<double>::quiet_NaN();
    for (size_t i = 1; i < history_.size(); ++i) {
        residuals_[i] = history_[i] - fitted_[i];
    }
    
    is_fitted_ = true;
}

core::Forecast IMAPA::predict(int horizon) {
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

void IMAPA::computeFittedValues() {
    // Very expensive: recompute IMAPA forecast for each time point
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
        
        // Compute mean interval for this subset
        double mean_interval_i = std::accumulate(intervals.begin(), intervals.end(), 0.0) / intervals.size();
        int max_agg_i = std::max(1, static_cast<int>(std::round(mean_interval_i)));
        
        // Compute forecasts at each level and average
        std::vector<double> forecasts_i;
        forecasts_i.reserve(max_agg_i);
        
        for (int agg_level = 1; agg_level <= max_agg_i; ++agg_level) {
            int lost_remainder = static_cast<int>(partial_history.size()) % agg_level;
            std::vector<double> y_cut;
            
            if (lost_remainder > 0 && partial_history.size() > static_cast<size_t>(lost_remainder)) {
                y_cut.assign(partial_history.begin() + lost_remainder, partial_history.end());
            } else if (lost_remainder == 0) {
                y_cut = partial_history;
            } else {
                continue;
            }
            
            auto agg_sums = utils::intermittent::chunkSums(y_cut, agg_level);
            if (agg_sums.empty()) {
                continue;
            }
            
            auto [forecast_agg, _] = utils::intermittent::optimizedSesForecasting(agg_sums);
            forecasts_i.push_back(forecast_agg / agg_level);
        }
        
        if (!forecasts_i.empty()) {
            fitted_[i] = std::accumulate(forecasts_i.begin(), forecasts_i.end(), 0.0) / forecasts_i.size();
        } else {
            fitted_[i] = 0.0;
        }
    }
}

} // namespace anofoxtime::models

