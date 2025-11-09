#include "anofox-time/models/auto_theta.hpp"
#include "anofox-time/utils/logging.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <chrono>
#include <stdexcept>

namespace anofoxtime::models {

namespace {

/**
 * @brief Compute autocorrelation function (ACF)
 * 
 * Computes ACF up to max_lag using standard formula.
 */
std::vector<double> computeACF(const std::vector<double>& data, int max_lag) {
    size_t n = data.size();
    if (n == 0 || max_lag < 1 || static_cast<size_t>(max_lag) >= n) {
        return {};
    }
    
    // Compute mean
    double mean = std::accumulate(data.begin(), data.end(), 0.0) / static_cast<double>(n);
    
    // Compute variance (lag 0 autocovariance)
    double var = 0.0;
    for (double val : data) {
        double diff = val - mean;
        var += diff * diff;
    }
    
    if (var < 1e-10) {
        return std::vector<double>(max_lag, 0.0);
    }
    
    // Compute ACF for each lag
    std::vector<double> acf(max_lag);
    for (int lag = 1; lag <= max_lag; ++lag) {
        double cov = 0.0;
        for (size_t i = 0; i < n - static_cast<size_t>(lag); ++i) {
            cov += (data[i] - mean) * (data[i + lag] - mean);
        }
        acf[lag - 1] = cov / var;
    }
    
    return acf;
}

/**
 * @brief Simple seasonal decomposition
 * 
 * Computes seasonal indices using moving average method.
 */
std::vector<double> decomposeSeasonalIndices(const std::vector<double>& data, int period,
                                              const std::string& type) {
    size_t n = data.size();
    if (n < static_cast<size_t>(period * 2)) {
        throw std::invalid_argument("Not enough data for seasonal decomposition");
    }
    
    // Compute seasonal indices by averaging values at each season position
    std::vector<double> seasonal(period, 0.0);
    std::vector<int> counts(period, 0);
    
    // Compute centered moving average to estimate trend
    std::vector<double> trend(n, 0.0);
    int half_window = period / 2;
    
    for (size_t i = half_window; i < n - half_window; ++i) {
        double sum = 0.0;
        for (int j = -half_window; j <= half_window; ++j) {
            sum += data[i + j];
        }
        trend[i] = sum / static_cast<double>(period);
    }
    
    // Fill in edges with first/last valid trend values
    for (int i = 0; i < half_window; ++i) {
        trend[i] = trend[half_window];
    }
    for (size_t i = n - half_window; i < n; ++i) {
        trend[i] = trend[n - half_window - 1];
    }
    
    // Extract seasonal component
    for (size_t i = 0; i < n; ++i) {
        int season_idx = i % period;
        double seasonal_val;
        
        if (type == "multiplicative") {
            if (std::abs(trend[i]) > 1e-10) {
                seasonal_val = data[i] / trend[i];
            } else {
                seasonal_val = 1.0;
            }
        } else {
            seasonal_val = data[i] - trend[i];
        }
        
        seasonal[season_idx] += seasonal_val;
        counts[season_idx]++;
    }
    
    // Average seasonal components
    for (int i = 0; i < period; ++i) {
        if (counts[i] > 0) {
            seasonal[i] /= static_cast<double>(counts[i]);
        }
    }
    
    // Normalize seasonal component
    if (type == "multiplicative") {
        double mean_seasonal = std::accumulate(seasonal.begin(), seasonal.end(), 0.0) / period;
        for (double& val : seasonal) {
            val /= mean_seasonal;
        }
    } else {
        double mean_seasonal = std::accumulate(seasonal.begin(), seasonal.end(), 0.0) / period;
        for (double& val : seasonal) {
            val -= mean_seasonal;
        }
    }
    
    return seasonal;
}

} // anonymous namespace

AutoTheta::AutoTheta(
    int seasonal_period,
    DecompositionType decomposition_type,
    const std::optional<std::string>& specific_model,
    int nmse)
    : seasonal_period_(seasonal_period),
      decomposition_type_(decomposition_type),
      specific_model_(specific_model),
      nmse_(nmse),
      best_model_type_(theta_pegels::ModelType::STM),
      best_alpha_(0.5),
      best_theta_(2.0),
      best_level_(0.0) {
    
    if (seasonal_period_ < 1) {
        throw std::invalid_argument("Seasonal period must be >= 1");
    }
    
    if (nmse_ < 1 || nmse_ > 30) {
        throw std::invalid_argument("nmse must be between 1 and 30");
    }
    
    // Validate specific_model if provided
    if (specific_model_) {
        const std::string& model = *specific_model_;
        if (model != "STM" && model != "OTM" && model != "DSTM" && model != "DOTM") {
            throw std::invalid_argument("Invalid model type: " + model);
        }
    }
}

bool AutoTheta::isConstantSeries(const std::vector<double>& data) const {
    if (data.empty()) return true;
    
    double first = data[0];
    for (size_t i = 1; i < data.size(); ++i) {
        if (std::abs(data[i] - first) > 1e-10) {
            return false;
        }
    }
    return true;
}

bool AutoTheta::detectSeasonality(const std::vector<double>& data, int period) {
    // Need at least 2 full seasons for reliable ACF
    if (data.size() < static_cast<size_t>(period * 2) || period < 4) {
        return false;
    }
    
    // Compute ACF
    auto acf = computeACF(data, period);
    if (acf.empty()) {
        return false;
    }
    
    // Test statistic: ACF at lag m
    double acf_m = acf[period - 1];
    
    // Compute standard error using Bartlett's formula
    // SE ≈ sqrt((1 + 2 * sum(r[1]^2 + ... + r[m-1]^2)) / n)
    double sum_sq = 0.0;
    for (int i = 0; i < period - 1; ++i) {
        sum_sq += acf[i] * acf[i];
    }
    
    double n = static_cast<double>(data.size());
    double se = std::sqrt((1.0 + 2.0 * sum_sq) / n);
    
    // Test at 95% confidence level (z = 1.645 for one-tailed test)
    double threshold = 1.645 * se;
    
    return std::abs(acf_m) > threshold;
}

std::string AutoTheta::selectDecompositionType(const std::vector<double>& data) {
    if (decomposition_type_ == DecompositionType::Additive) {
        return "additive";
    } else if (decomposition_type_ == DecompositionType::Multiplicative) {
        return "multiplicative";
    }
    
    // Auto: use multiplicative if all data is positive
    double min_val = *std::min_element(data.begin(), data.end());
    return (min_val > 0.0) ? "multiplicative" : "additive";
}

void AutoTheta::performDecomposition(const std::vector<double>& data) {
    actual_decomposition_type_ = selectDecompositionType(data);
    
    ANOFOX_DEBUG("AutoTheta: Performing {} decomposition", actual_decomposition_type_);
    
    // Compute seasonal indices
    seasonal_indices_ = decomposeSeasonalIndices(data, seasonal_period_, actual_decomposition_type_);
    
    // Deseasonalize data
    deseasonalized_data_.resize(data.size());
    for (size_t i = 0; i < data.size(); ++i) {
        int season_idx = i % seasonal_period_;
        if (actual_decomposition_type_ == "multiplicative") {
            deseasonalized_data_[i] = data[i] / seasonal_indices_[season_idx];
        } else {
            deseasonalized_data_[i] = data[i] - seasonal_indices_[season_idx];
        }
    }
    
    decomposed_ = true;
}

theta_pegels::OptimResult AutoTheta::fitModel(
    theta_pegels::ModelType model_type,
    const std::vector<double>& data) {
    
    // Initial parameters
    double init_level = data[0];
    double init_alpha = 0.5;
    double init_theta = 2.0;
    
    // Determine what to optimize based on model type
    bool opt_level = false;
    bool opt_alpha = false;
    bool opt_theta = false;
    
    switch (model_type) {
        case theta_pegels::ModelType::STM:
            // Standard Theta: no optimization, fixed theta=2
            opt_theta = false;
            opt_alpha = false;
            opt_level = false;
            break;
            
        case theta_pegels::ModelType::OTM:
            // Optimized Theta: optimize level, alpha, and theta
            opt_level = true;
            opt_alpha = true;
            opt_theta = true;
            break;
            
        case theta_pegels::ModelType::DSTM:
            // Dynamic Standard Theta: fixed theta=2, no optimization
            opt_theta = false;
            opt_alpha = false;
            opt_level = false;
            break;
            
        case theta_pegels::ModelType::DOTM:
            // Dynamic Optimized Theta: optimize level, alpha, and theta
            opt_level = true;
            opt_alpha = true;
            opt_theta = true;
            break;
    }
    
    // Run optimization with L-BFGS (default, much faster)
    return theta_pegels::optimize(
        data, model_type,
        opt_level, opt_alpha, opt_theta,
        init_level, init_alpha, init_theta,
        nmse_,
        theta_pegels::OptimizerType::LBFGS
    );
}

void AutoTheta::fit(const core::TimeSeries& ts) {
    if (ts.dimensions() != 1) {
        throw std::invalid_argument("AutoTheta currently supports univariate series only");
    }
    
    original_data_ = ts.getValues();
    
    if (original_data_.empty()) {
        throw std::invalid_argument("Cannot fit AutoTheta on empty time series");
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Check for constant series
    if (isConstantSeries(original_data_)) {
        diagnostics_.constant_series = true;
        diagnostics_.best_model_type = "STM";
        diagnostics_.best_mse = 0.0;
        diagnostics_.models_evaluated = 1;
        
        best_model_type_ = theta_pegels::ModelType::STM;
        best_alpha_ = 0.5;
        best_theta_ = 2.0;
        best_level_ = original_data_[0] / 2.0;
        
        ANOFOX_INFO("AutoTheta: Detected constant series, using STM");
        is_fitted_ = true;
        return;
    }
    
    // Check if we need to handle tiny datasets
    if (original_data_.size() <= 3) {
        throw std::invalid_argument("AutoTheta requires at least 4 observations");
    }
    
    // Seasonality detection and decomposition
    const std::vector<double>* data_to_fit = &original_data_;
    
    if (seasonal_period_ >= 4 && original_data_.size() >= static_cast<size_t>(seasonal_period_ * 2)) {
        diagnostics_.seasonality_detected = detectSeasonality(original_data_, seasonal_period_);
        
        if (diagnostics_.seasonality_detected) {
            ANOFOX_INFO("AutoTheta: Seasonality detected, performing decomposition");
            performDecomposition(original_data_);
            data_to_fit = &deseasonalized_data_;
            diagnostics_.decomposition_used = actual_decomposition_type_;
        }
    }
    
    // Determine which models to try
    std::vector<theta_pegels::ModelType> models_to_try;
    
    if (specific_model_) {
        // User specified a specific model
        const std::string& model = *specific_model_;
        if (model == "STM") {
            models_to_try.push_back(theta_pegels::ModelType::STM);
        } else if (model == "OTM") {
            models_to_try.push_back(theta_pegels::ModelType::OTM);
        } else if (model == "DSTM") {
            models_to_try.push_back(theta_pegels::ModelType::DSTM);
        } else if (model == "DOTM") {
            models_to_try.push_back(theta_pegels::ModelType::DOTM);
        }
    } else {
        // Try all models
        models_to_try = {
            theta_pegels::ModelType::STM,
            theta_pegels::ModelType::OTM,
            theta_pegels::ModelType::DSTM,
            theta_pegels::ModelType::DOTM
        };
    }
    
    // Fit each candidate model and track the best
    double best_mse = std::numeric_limits<double>::infinity();
    
    for (auto model_type : models_to_try) {
        try {
            auto result = fitModel(model_type, *data_to_fit);
            diagnostics_.models_evaluated++;
            
            if (!std::isnan(result.mse) && std::isfinite(result.mse) && result.mse < best_mse) {
                best_mse = result.mse;
                best_model_type_ = model_type;
                best_alpha_ = result.alpha;
                best_theta_ = result.theta;
                best_level_ = result.level;
                
                // Store states for prediction
                best_states_.resize(data_to_fit->size());
                std::vector<double> e(data_to_fit->size());
                std::vector<double> amse(nmse_);
                theta_pegels::calc(*data_to_fit, best_states_, model_type,
                                  best_level_, best_alpha_, best_theta_, e, amse, nmse_);
                
                // Determine model name
                switch (model_type) {
                    case theta_pegels::ModelType::STM:
                        diagnostics_.best_model_type = "STM";
                        break;
                    case theta_pegels::ModelType::OTM:
                        diagnostics_.best_model_type = "OTM";
                        break;
                    case theta_pegels::ModelType::DSTM:
                        diagnostics_.best_model_type = "DSTM";
                        break;
                    case theta_pegels::ModelType::DOTM:
                        diagnostics_.best_model_type = "DOTM";
                        break;
                }
                
                ANOFOX_DEBUG("AutoTheta: {} - MSE={:.6f}, α={:.4f}, θ={:.2f}",
                           diagnostics_.best_model_type, result.mse, result.alpha, result.theta);
            }
        } catch (const std::exception& e) {
            ANOFOX_WARN("AutoTheta: Failed to fit model: {}", e.what());
            continue;
        }
    }
    
    if (!std::isfinite(best_mse)) {
        throw std::runtime_error("AutoTheta: No model could be successfully fitted");
    }
    
    diagnostics_.best_mse = best_mse;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    diagnostics_.optimization_time_ms =
        std::chrono::duration<double, std::milli>(end_time - start_time).count();
    
    ANOFOX_INFO("AutoTheta: Selected {} with MSE={:.6f} (evaluated {} models in {:.2f}ms)",
               diagnostics_.best_model_type, diagnostics_.best_mse,
               diagnostics_.models_evaluated, diagnostics_.optimization_time_ms);
    
    is_fitted_ = true;
}

std::vector<double> AutoTheta::getSeasonalForecast(int horizon) const {
    std::vector<double> seasonal_forecast(horizon);
    for (int i = 0; i < horizon; ++i) {
        int season_idx = (original_data_.size() + i) % seasonal_period_;
        seasonal_forecast[i] = seasonal_indices_[season_idx];
    }
    return seasonal_forecast;
}

std::vector<double> AutoTheta::reseasonalize(const std::vector<double>& forecast) const {
    if (!decomposed_) {
        return forecast;
    }
    
    auto seasonal_forecast = getSeasonalForecast(static_cast<int>(forecast.size()));
    std::vector<double> reseasonalized(forecast.size());
    
    for (size_t i = 0; i < forecast.size(); ++i) {
        if (actual_decomposition_type_ == "multiplicative") {
            reseasonalized[i] = forecast[i] * seasonal_forecast[i];
        } else {
            reseasonalized[i] = forecast[i] + seasonal_forecast[i];
        }
    }
    
    return reseasonalized;
}

core::Forecast AutoTheta::predict(int horizon) {
    if (!is_fitted_) {
        throw std::runtime_error("AutoTheta::predict called before fit");
    }
    
    if (horizon <= 0) {
        throw std::invalid_argument("Forecast horizon must be positive");
    }
    
    // Generate forecast using best model
    std::vector<double> forecast_values(horizon);
    theta_pegels::forecast(best_states_, best_states_.size(), best_model_type_,
                          forecast_values, best_alpha_, best_theta_);
    
    // Reseasonalize if needed
    forecast_values = reseasonalize(forecast_values);
    
    // Create Forecast object
    core::Forecast forecast;
    forecast.primary() = std::move(forecast_values);
    return forecast;
}

core::Forecast AutoTheta::predictWithConfidence(int horizon, double confidence) {
    if (!is_fitted_) {
        throw std::runtime_error("AutoTheta::predictWithConfidence called before fit");
    }
    
    // Get point forecast
    auto point_forecast = predict(horizon);
    
    // Compute residuals for confidence interval estimation
    const std::vector<double>* fit_data = decomposed_ ? &deseasonalized_data_ : &original_data_;
    
    std::vector<double> residuals(fit_data->size());
    theta_pegels::StateMatrix states(fit_data->size());
    std::vector<double> amse(nmse_);
    theta_pegels::calc(*fit_data, states, best_model_type_,
                      best_level_, best_alpha_, best_theta_, residuals, amse, nmse_);
    
    // Compute residual standard deviation (skip first 3 observations)
    double sum_sq = 0.0;
    size_t count = 0;
    for (size_t i = 3; i < residuals.size(); ++i) {
        sum_sq += residuals[i] * residuals[i];
        count++;
    }
    double sigma = std::sqrt(sum_sq / static_cast<double>(count));
    
    // Compute confidence intervals (assuming normal distribution)
    double z_score = 1.96;  // 95% confidence
    if (std::abs(confidence - 0.90) < 0.01) {
        z_score = 1.645;
    } else if (std::abs(confidence - 0.99) < 0.01) {
        z_score = 2.576;
    }
    
    std::vector<double> lower(horizon);
    std::vector<double> upper(horizon);
    
    for (int h = 0; h < horizon; ++h) {
        double se = sigma * std::sqrt(h + 1);  // SE grows with horizon
        lower[h] = point_forecast.primary()[h] - z_score * se;
        upper[h] = point_forecast.primary()[h] + z_score * se;
    }
    
    // Create Forecast with confidence intervals
    core::Forecast result;
    result.primary() = point_forecast.primary();
    result.lowerSeries() = std::move(lower);
    result.upperSeries() = std::move(upper);
    return result;
}

const std::vector<double>& AutoTheta::fittedValues() const {
    static const std::vector<double> empty;
    if (!is_fitted_) {
        return empty;
    }
    // Return deseasonalized fitted values if decomposed, otherwise original
    return decomposed_ ? deseasonalized_data_ : original_data_;
}

const std::vector<double>& AutoTheta::residuals() const {
    static const std::vector<double> empty;
    if (!is_fitted_) {
        return empty;
    }
    // For simplicity, return empty - residuals can be computed on demand
    return empty;
}

double AutoTheta::getOptimalAlpha() const {
    if (!is_fitted_) {
        throw std::runtime_error("AutoTheta not fitted");
    }
    return best_alpha_;
}

double AutoTheta::getOptimalTheta() const {
    if (!is_fitted_) {
        throw std::runtime_error("AutoTheta not fitted");
    }
    return best_theta_;
}

double AutoTheta::getOptimalLevel() const {
    if (!is_fitted_) {
        throw std::runtime_error("AutoTheta not fitted");
    }
    return best_level_;
}

} // namespace anofoxtime::models

