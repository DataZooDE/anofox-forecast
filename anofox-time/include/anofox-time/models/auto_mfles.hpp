#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/mfles.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>
#include <memory>

namespace anofoxtime::models {

/**
 * @brief AutoMFLES - Automatic parameter optimization for MFLES
 * 
 * AutoMFLES automatically selects the optimal learning rates and number of
 * iterations for the MFLES model using AIC-based model selection.
 * 
 * The optimization process:
 * 1. Grid search over learning rate combinations
 * 2. Test different iteration counts (1-7)
 * 3. Select model with lowest AIC
 * 
 * This provides automatic tuning while maintaining MFLES's speed advantage.
 */
class AutoMFLES : public IForecaster {
public:
	/**
	 * @brief Construct an AutoMFLES forecaster
	 * @param seasonal_periods Vector of seasonal periods (e.g., {12} for monthly)
	 * @param test_size Number of points to hold out for validation (default: 0, uses AIC)
	 */
	explicit AutoMFLES(std::vector<int> seasonal_periods = {12}, int test_size = 0);
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	
	std::string getName() const override {
		return "AutoMFLES";
	}
	
	// Accessors for selected model
	const MFLES& selectedModel() const {
		if (!fitted_model_) {
			throw std::runtime_error("AutoMFLES: Must call fit() before accessing selected model");
		}
		return *fitted_model_;
	}
	
	const std::vector<int>& seasonalPeriods() const {
		return seasonal_periods_;
	}
	
	// Get selected parameters
	int selectedIterations() const { return best_iterations_; }
	double selectedTrendLR() const { return best_lr_trend_; }
	double selectedSeasonLR() const { return best_lr_season_; }
	double selectedLevelLR() const { return best_lr_level_; }
	double selectedAIC() const { return best_aic_; }
	
	// Diagnostics
	struct OptimizationDiagnostics {
		int models_evaluated = 0;
		double best_aic = 0.0;
		int best_iterations = 0;
		double best_lr_trend = 0.0;
		double best_lr_season = 0.0;
		double best_lr_level = 0.0;
		double optimization_time_ms = 0.0;
	};
	
	const OptimizationDiagnostics& diagnostics() const {
		return diagnostics_;
	}

private:
	// Configuration
	std::vector<int> seasonal_periods_;
	int test_size_;
	
	// Selected parameters
	int best_iterations_ = 3;
	double best_lr_trend_ = 0.3;
	double best_lr_season_ = 0.5;
	double best_lr_level_ = 0.8;
	double best_aic_ = std::numeric_limits<double>::infinity();
	
	// Fitted model
	std::unique_ptr<MFLES> fitted_model_;
	OptimizationDiagnostics diagnostics_;
	
	// Optimization
	struct CandidateConfig {
		int iterations;
		double lr_trend;
		double lr_season;
		double lr_level;
		double aic;
		
		bool operator<(const CandidateConfig& other) const {
			return aic < other.aic;
		}
	};
	
	void optimizeParameters(const core::TimeSeries& ts);
	double computeAIC(const MFLES& model, int n, int k) const;
	std::vector<CandidateConfig> generateCandidates() const;
};

/**
 * @brief Builder for AutoMFLES forecaster
 */
class AutoMFLESBuilder {
public:
	AutoMFLESBuilder() = default;
	
	AutoMFLESBuilder& withSeasonalPeriods(std::vector<int> periods) {
		seasonal_periods_ = std::move(periods);
		return *this;
	}
	
	AutoMFLESBuilder& withTestSize(int test_size) {
		test_size_ = test_size;
		return *this;
	}
	
	std::unique_ptr<AutoMFLES> build() {
		return std::make_unique<AutoMFLES>(seasonal_periods_, test_size_);
	}

private:
	std::vector<int> seasonal_periods_ = {12};
	int test_size_ = 0;
};

} // namespace anofoxtime::models

