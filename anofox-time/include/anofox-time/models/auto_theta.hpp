#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/theta_pegels.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <memory>
#include <string>
#include <optional>

namespace anofoxtime::models {

/**
 * @brief Automatic Theta Method model selection
 * 
 * AutoTheta automatically selects the best Theta variant for a given time series:
 * - STM: Standard Theta Method (fixed θ=2)
 * - OTM: Optimized Theta Method (optimizes θ and α)
 * - DSTM: Dynamic Standard Theta Method (dynamic updates)
 * - DOTM: Dynamic Optimized Theta Method (M4 competition winner component)
 * 
 * The model also handles:
 * - Automatic seasonality detection using ACF test
 * - Seasonal decomposition (additive/multiplicative)
 * - Model selection based on MSE
 * 
 * Reference: Nixtla's statsforecast auto_theta implementation
 */
class AutoTheta : public IForecaster {
public:
    /**
     * @brief Decomposition type for seasonal data
     */
    enum class DecompositionType {
        Additive,        // y = trend + seasonal + noise
        Multiplicative,  // y = trend * seasonal * noise
        Auto             // Automatically select based on data characteristics
    };
    
    /**
     * @brief Diagnostics information from model selection
     */
    struct Diagnostics {
        int models_evaluated = 0;
        std::string best_model_type;
        double best_mse = std::numeric_limits<double>::infinity();
        double optimization_time_ms = 0.0;
        bool seasonality_detected = false;
        std::string decomposition_used;
        bool constant_series = false;
    };
    
    /**
     * @brief Constructor
     * 
     * @param seasonal_period Seasonal period (1 for non-seasonal)
     * @param decomposition_type How to handle seasonality (default: Auto)
     * @param specific_model If provided, only fit this model type (e.g., "OTM")
     * @param nmse Number of steps for multi-step MSE evaluation (default: 3)
     */
    explicit AutoTheta(
        int seasonal_period = 1,
        DecompositionType decomposition_type = DecompositionType::Auto,
        const std::optional<std::string>& specific_model = std::nullopt,
        int nmse = 3
    );
    
    void fit(const core::TimeSeries& ts) override;
    core::Forecast predict(int horizon) override;
    core::Forecast predictWithConfidence(int horizon, double confidence);
    
    std::string getName() const override {
        return "AutoTheta";
    }
    
    // Accessors
    const Diagnostics& getDiagnostics() const { return diagnostics_; }
    std::string getSelectedModel() const { return diagnostics_.best_model_type; }
    const std::vector<double>& fittedValues() const;
    const std::vector<double>& residuals() const;
    
    // Get optimal parameters from selected model
    double getOptimalAlpha() const;
    double getOptimalTheta() const;
    double getOptimalLevel() const;
    
private:
    int seasonal_period_;
    DecompositionType decomposition_type_;
    std::optional<std::string> specific_model_;
    int nmse_;
    
    Diagnostics diagnostics_;
    bool is_fitted_ = false;
    
    // Deseasonalized data and seasonal components
    std::vector<double> original_data_;
    std::vector<double> deseasonalized_data_;
    std::vector<double> seasonal_indices_;
    bool decomposed_ = false;
    std::string actual_decomposition_type_;
    
    // Best fitted model
    theta_pegels::ModelType best_model_type_;
    double best_alpha_;
    double best_theta_;
    double best_level_;
    theta_pegels::StateMatrix best_states_;
    
    // Helper methods
    
    /**
     * @brief Detect seasonality using ACF test
     * 
     * Tests if the autocorrelation at lag m is significantly different from zero.
     * Uses the Box-Pierce test statistic.
     */
    bool detectSeasonality(const std::vector<double>& data, int period);
    
    /**
     * @brief Perform seasonal decomposition
     * 
     * Decomposes the time series into trend, seasonal, and residual components.
     */
    void performDecomposition(const std::vector<double>& data);
    
    /**
     * @brief Select decomposition type based on data
     * 
     * Multiplicative if all data is positive, otherwise additive.
     */
    std::string selectDecompositionType(const std::vector<double>& data);
    
    /**
     * @brief Check if series is constant
     */
    bool isConstantSeries(const std::vector<double>& data) const;
    
    /**
     * @brief Fit a specific Theta model variant
     */
    theta_pegels::OptimResult fitModel(
        theta_pegels::ModelType model_type,
        const std::vector<double>& data
    );
    
    /**
     * @brief Reseasonalize forecast if decomposition was used
     */
    std::vector<double> reseasonalize(const std::vector<double>& forecast) const;
    
    /**
     * @brief Extract seasonal forecast for future periods
     */
    std::vector<double> getSeasonalForecast(int horizon) const;
};

} // namespace anofoxtime::models

