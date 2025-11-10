#include "anofox-time/models/theta.hpp"
#include "anofox-time/models/theta_utils.hpp"
#include "anofox-time/utils/logging.hpp"
#include <cmath>
#include <numeric>
#include <algorithm>
#include <stdexcept>

namespace anofoxtime::models {

namespace {
    constexpr double kEpsilon = 1e-10;
}

Theta::Theta(int seasonal_period, double theta_param)
    : seasonal_period_(seasonal_period), theta_(theta_param),
      alpha_(0.5), level_(0.0) {
    if (seasonal_period_ < 1) {
        throw std::invalid_argument("Seasonal period must be >= 1");
    }
    if (theta_ <= 0.0) {
        throw std::invalid_argument("Theta parameter must be positive");
    }
}

std::vector<double> Theta::deseasonalize(const std::vector<double>& data) {
    std::vector<double> deseasonalized;
    theta_utils::deseasonalize(data, seasonal_period_, deseasonalized, seasonal_indices_);
    return deseasonalized;
}

std::vector<double> Theta::reseasonalize(const std::vector<double>& forecast) const {
    std::vector<double> reseasonalized;
    theta_utils::reseasonalize(forecast, seasonal_indices_, seasonal_period_, 
                               history_.size(), reseasonalized);
    return reseasonalized;
}

void Theta::fitRaw(const std::vector<double>& data) {
    if (data.empty()) {
        throw std::invalid_argument("Cannot fit Theta on empty data");
    }
    
    history_ = data;
    deseasonalized_ = deseasonalize(history_);
    
    // Use Pegels state-space formulation
    // Initial smoothed value is the first observation
    double initial_smoothed = deseasonalized_[0];
    
    // For STM (Standard Theta Method): use fixed alpha and theta
    // Alpha will be set externally or defaults to 0.5
    // Theta is set in constructor (default 2.0)
    
    states_.resize(deseasonalized_.size());
    std::vector<double> e(deseasonalized_.size());
    std::vector<double> amse(3);  // 3-step-ahead MSE
    
    // Calculate with Pegels formulation
    theta_pegels::calc(deseasonalized_, states_, 
                      theta_pegels::ModelType::STM,
                      initial_smoothed, alpha_, theta_,
                      e, amse, 3);
    
    // Store final level
    level_ = states_.back()[0];
    
    // Compute fitted values and residuals
    computeFittedValues();
    
    is_fitted_ = true;
    
    ANOFOX_INFO("Theta fitted with alpha={:.4f}, theta={:.2f}", alpha_, theta_);
}

void Theta::fit(const core::TimeSeries& ts) {
    if (ts.dimensions() != 1) {
        throw std::invalid_argument("Theta currently supports univariate series only");
    }
    
    fitRaw(ts.getValues());
}

void Theta::computeFittedValues() {
    const size_t n = deseasonalized_.size();
    fitted_.resize(n);
    residuals_.resize(n);
    
    // Fitted values from mu component
    for (size_t i = 0; i < n; ++i) {
        fitted_[i] = states_[i][4];  // mu is the forecast
        residuals_[i] = deseasonalized_[i] - fitted_[i];
    }
    
    // Reseasonalize if needed
    if (seasonal_period_ > 1 && !seasonal_indices_.empty()) {
        for (size_t i = 0; i < n; ++i) {
            size_t season_idx = i % static_cast<size_t>(seasonal_period_);
            fitted_[i] *= seasonal_indices_[season_idx];
            residuals_[i] = history_[i] - fitted_[i];
        }
    }
}

core::Forecast Theta::predict(int horizon) {
    if (!is_fitted_) {
        throw std::runtime_error("Theta::predict called before fit");
    }
    
    if (horizon <= 0) {
        return {};
    }
    
    // Generate forecast using Pegels formulation
    std::vector<double> forecast(horizon);
    theta_pegels::StateMatrix workspace(states_.size() + horizon);
    theta_pegels::forecast(states_, states_.size(), 
                          theta_pegels::ModelType::STM,
                          forecast, alpha_, theta_, workspace);
    
    // Reseasonalize
    forecast = reseasonalize(forecast);
    
    core::Forecast result;
    result.primary() = forecast;
    return result;
}

core::Forecast Theta::predictWithConfidence(int horizon, double confidence) {
    if (confidence <= 0.0 || confidence >= 1.0) {
        throw std::invalid_argument("Confidence level must be between 0 and 1");
    }
    
    auto forecast = predict(horizon);
    
    if (residuals_.empty()) {
        return forecast;
    }
    
    // Compute residual variance
    double sum_sq = 0.0;
    for (double r : residuals_) {
        sum_sq += r * r;
    }
    const double sigma = std::sqrt(sum_sq / static_cast<double>(residuals_.size()));
    
    // Normal quantile for confidence interval
    const double z = 1.96;  // Approximate 95% CI
    
    // Compute confidence intervals
    auto& lower = forecast.lowerSeries();
    auto& upper = forecast.upperSeries();
    lower.resize(horizon);
    upper.resize(horizon);
    
    for (int h = 0; h < horizon; ++h) {
        const double std_h = sigma * std::sqrt(static_cast<double>(h + 1));
        lower[h] = forecast.primary()[h] - z * std_h;
        upper[h] = forecast.primary()[h] + z * std_h;
    }
    
    return forecast;
}

} // namespace anofoxtime::models

