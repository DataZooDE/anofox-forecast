#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/mfles.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include "anofox-time/utils/cross_validation.hpp"
#include <vector>
#include <string>
#include <memory>

namespace anofoxtime::models {

/**
 * @brief AutoMFLES v2 - CV-based hyperparameter optimization for MFLES v2
 *
 * Uses cross-validation to automatically select optimal MFLES parameters:
 * - Trend method (OLS, Siegel Robust, Piecewise)
 * - Number of Fourier terms
 * - Number of boosting rounds
 * - ES ensemble alpha range
 *
 * This replaces heuristic tuning with data-driven optimization.
 */
class AutoMFLES : public IForecaster {
public:
	/**
	 * @brief Configuration for AutoMFLES optimization
	 */
	struct Config {
		// CV settings
		int cv_horizon = 6;              // Forecast horizon for CV
		int cv_initial_window = 50;      // Initial training window
		int cv_step = 6;                 // Step between folds
		utils::CVStrategy cv_strategy = utils::CVStrategy::ROLLING;

		// Search space
		std::vector<TrendMethod> trend_methods = {
			TrendMethod::OLS,
			TrendMethod::SIEGEL_ROBUST
		};
		std::vector<int> max_fourier_orders = {3, 5, 7};
		std::vector<int> max_rounds_options = {3, 5, 7, 10};

		// Fixed parameters
		std::vector<int> seasonal_periods = {12};
		double min_alpha = 0.1;
		double max_alpha = 0.9;
		int es_ensemble_size = 10;
	};

	AutoMFLES();
	explicit AutoMFLES(const Config& config);

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

	// Get selected parameters
	TrendMethod selectedTrendMethod() const { return best_trend_method_; }
	int selectedFourierOrder() const { return best_fourier_order_; }
	int selectedMaxRounds() const { return best_max_rounds_; }
	double selectedCV_MAE() const { return best_cv_mae_; }

	// Diagnostics
	struct OptimizationDiagnostics {
		int configs_evaluated = 0;
		double best_cv_mae = 0.0;
		TrendMethod best_trend_method = TrendMethod::OLS;
		int best_fourier_order = 3;
		int best_max_rounds = 5;
		double optimization_time_ms = 0.0;
	};

	const OptimizationDiagnostics& diagnostics() const {
		return diagnostics_;
	}

private:
	Config config_;

	// Selected parameters
	TrendMethod best_trend_method_ = TrendMethod::OLS;
	int best_fourier_order_ = 3;
	int best_max_rounds_ = 5;
	double best_cv_mae_ = std::numeric_limits<double>::infinity();

	// Fitted model
	std::unique_ptr<MFLES> fitted_model_;
	OptimizationDiagnostics diagnostics_;

	// Optimization
	struct CandidateConfig {
		TrendMethod trend_method;
		int fourier_order;
		int max_rounds;
		double cv_mae;

		bool operator<(const CandidateConfig& other) const {
			return cv_mae < other.cv_mae;
		}
	};

	void optimizeParameters(const core::TimeSeries& ts);
	double evaluateConfig(const core::TimeSeries& ts, const MFLES::Params& params);
	std::vector<CandidateConfig> generateCandidates() const;
};

} // namespace anofoxtime::models
