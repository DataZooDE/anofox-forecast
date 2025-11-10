#include "anofox-time/models/optimized_theta.hpp"
#include "anofox-time/models/theta_utils.hpp"
#include "anofox-time/utils/logging.hpp"
#include <stdexcept>

namespace anofoxtime::models {

OptimizedTheta::OptimizedTheta(int seasonal_period)
    : seasonal_period_(seasonal_period),
      optimal_alpha_(0.5),
      optimal_theta_(2.0),
      optimal_level_(0.0),
      optimal_mse_(std::numeric_limits<double>::infinity()) {
    if (seasonal_period_ < 1) {
        throw std::invalid_argument("Seasonal period must be >= 1");
    }
}

void OptimizedTheta::fit(const core::TimeSeries& ts) {
    if (ts.dimensions() != 1) {
        throw std::invalid_argument("OptimizedTheta currently supports univariate series only");
    }
    
    const auto data = ts.getValues();
    
    if (data.empty()) {
        throw std::invalid_argument("Cannot fit OptimizedTheta on empty time series");
    }
    
    // Deseasonalize data directly without creating temporary model
    std::vector<double> deseasonalized;
    std::vector<double> seasonal_indices;
    theta_utils::deseasonalize(data, seasonal_period_, deseasonalized, seasonal_indices);
    
    // Optimize using Pegels state-space
    // OTM optimizes: level (initial smoothed), alpha, and theta
    double init_level = deseasonalized[0];
    double init_alpha = 0.5;
    double init_theta = 2.0;
    
    ANOFOX_INFO("OptimizedTheta: Starting parameter optimization");
    
    auto result = theta_pegels::optimize(
        deseasonalized,
        theta_pegels::ModelType::OTM,  // Optimized Theta Method
        true,   // Optimize level
        true,   // Optimize alpha
        true,   // Optimize theta
        init_level,
        init_alpha,
        init_theta,
        3  // 3-step-ahead MSE
    );
    
    optimal_alpha_ = result.alpha;
    optimal_theta_ = result.theta;
    optimal_level_ = result.level;
    optimal_mse_ = result.mse;
    
    ANOFOX_INFO("OptimizedTheta: Optimal parameters - alpha={:.4f}, theta={:.2f}, level={:.2f}, MSE={:.4f}",
                optimal_alpha_, optimal_theta_, optimal_level_, optimal_mse_);
    
    // Fit final model with optimal parameters
    fitted_model_ = std::make_unique<Theta>(seasonal_period_, optimal_theta_);
    fitted_model_->setAlpha(optimal_alpha_);
    fitted_model_->fitRaw(data);
    
    is_fitted_ = true;
}

core::Forecast OptimizedTheta::predict(int horizon) {
    if (!is_fitted_ || !fitted_model_) {
        throw std::runtime_error("OptimizedTheta::predict called before fit");
    }
    
    return fitted_model_->predict(horizon);
}

core::Forecast OptimizedTheta::predictWithConfidence(int horizon, double confidence) {
    if (!is_fitted_ || !fitted_model_) {
        throw std::runtime_error("OptimizedTheta::predictWithConfidence called before fit");
    }
    
    return fitted_model_->predictWithConfidence(horizon, confidence);
}

const std::vector<double>& OptimizedTheta::fittedValues() const {
    if (!fitted_model_) {
        static const std::vector<double> empty;
        return empty;
    }
    return fitted_model_->fittedValues();
}

const std::vector<double>& OptimizedTheta::residuals() const {
    if (!fitted_model_) {
        static const std::vector<double> empty;
        return empty;
    }
    return fitted_model_->residuals();
}

} // namespace anofoxtime::models

