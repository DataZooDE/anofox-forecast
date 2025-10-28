#include "anofox-time/models/dynamic_optimized_theta.hpp"
#include "anofox-time/utils/logging.hpp"
#include <stdexcept>

namespace anofoxtime::models {

DynamicOptimizedTheta::DynamicOptimizedTheta(int seasonal_period)
    : seasonal_period_(seasonal_period),
      optimal_alpha_(0.5),
      optimal_theta_(2.0),
      optimal_level_(0.0),
      optimal_mse_(std::numeric_limits<double>::infinity()) {
    if (seasonal_period_ < 1) {
        throw std::invalid_argument("Seasonal period must be >= 1");
    }
}

void DynamicOptimizedTheta::fit(const core::TimeSeries& ts) {
    if (ts.dimensions() != 1) {
        throw std::invalid_argument("DynamicOptimizedTheta currently supports univariate series only");
    }
    
    const auto data = ts.getValues();
    
    if (data.empty()) {
        throw std::invalid_argument("Cannot fit DynamicOptimizedTheta on empty time series");
    }
    
    // First do a basic DynamicTheta fit to get initial estimates
    DynamicTheta temp_model(seasonal_period_, 2.0);
    temp_model.fitRaw(data);
    auto deseasonalized = temp_model.deseasonalize(data);
    
    // Optimize using Pegels state-space with DYNAMIC An/Bn
    double init_level = deseasonalized[0];
    double init_alpha = 0.5;
    double init_theta = 2.0;
    
    ANOFOX_INFO("DynamicOptimizedTheta: Starting parameter optimization");
    
    auto result = theta_pegels::optimize(
        deseasonalized,
        theta_pegels::ModelType::DOTM,  // Dynamic Optimized Theta Method
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
    
    ANOFOX_INFO("DynamicOptimizedTheta: Optimal parameters - alpha={:.4f}, theta={:.2f}, level={:.2f}, MSE={:.4f}",
                optimal_alpha_, optimal_theta_, optimal_level_, optimal_mse_);
    
    // Fit final model with optimal parameters
    fitted_model_ = std::make_unique<DynamicTheta>(seasonal_period_, optimal_theta_);
    fitted_model_->setAlpha(optimal_alpha_);
    fitted_model_->fitRaw(data);
    
    is_fitted_ = true;
}

core::Forecast DynamicOptimizedTheta::predict(int horizon) {
    if (!is_fitted_ || !fitted_model_) {
        throw std::runtime_error("DynamicOptimizedTheta::predict called before fit");
    }
    
    return fitted_model_->predict(horizon);
}

core::Forecast DynamicOptimizedTheta::predictWithConfidence(int horizon, double confidence) {
    if (!is_fitted_ || !fitted_model_) {
        throw std::runtime_error("DynamicOptimizedTheta::predictWithConfidence called before fit");
    }
    
    return fitted_model_->predictWithConfidence(horizon, confidence);
}

const std::vector<double>& DynamicOptimizedTheta::fittedValues() const {
    if (!fitted_model_) {
        static const std::vector<double> empty;
        return empty;
    }
    return fitted_model_->fittedValues();
}

const std::vector<double>& DynamicOptimizedTheta::residuals() const {
    if (!fitted_model_) {
        static const std::vector<double> empty;
        return empty;
    }
    return fitted_model_->residuals();
}

} // namespace anofoxtime::models

