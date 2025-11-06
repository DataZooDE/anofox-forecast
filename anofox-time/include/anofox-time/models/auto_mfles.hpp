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
	 * Matches statsforecast MFLES grid search for apple-to-apple comparison
	 */
	struct Config {
		// CV settings (statsforecast parameters: test_size, n_windows)
		// test_size: Forecast horizon used during CV - statsforecast recommends season_length or season_length/2
		//            Set to -1 to auto-detect from seasonal_periods (uses first period)
		int cv_horizon = -1;             // -1 = auto (use seasonal_periods[0]), or explicit value
		int cv_n_windows = 2;            // Number of CV folds (statsforecast default: 2)
		int cv_initial_window = -1;      // Initial training window (-1 = auto: 10 * cv_horizon)
		int cv_step = -1;                // Step between folds (-1 = auto: cv_horizon)
		utils::CVStrategy cv_strategy = utils::CVStrategy::ROLLING;

		// Statsforecast grid search parameters (24 configurations: 2×2×3×2)
		std::vector<bool> seasonality_weights_options = {false, true};  // Time-varying seasonal weights
		std::vector<bool> smoother_options = {false, true};             // ES ensemble (false) vs MA (true)
		std::vector<int> ma_window_options = {-1, -2, -3};              // -1=period, -2=period/2, -3=None (ES ensemble)
		std::vector<bool> seasonal_period_options = {false, true};      // false=None (no seasonality), true=use period

		// Fixed parameters (not optimized by statsforecast grid search)
		std::vector<int> seasonal_periods = {12};
		int max_rounds = 10;             // Tuned default (was 50 in statsforecast)
		TrendMethod trend_method = TrendMethod::OLS;  // statsforecast uses OLS
		int fourier_order = -1;          // statsforecast uses adaptive
		double min_alpha = 0.05;         // statsforecast default
		double max_alpha = 1.0;          // statsforecast default
		int es_ensemble_size = 20;       // statsforecast default

		// Learning rates (tuned defaults for best accuracy)
		double lr_trend = 0.3;           // Trend learning rate (was 0.9 in statsforecast)
		double lr_season = 0.5;          // Seasonal learning rate (was 0.9 in statsforecast)
		double lr_rs = 0.8;              // Residual smoothing learning rate (was 1.0 in statsforecast)
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

	// Get selected parameters (statsforecast grid)
	bool selectedSeasonalityWeights() const { return best_seasonality_weights_; }
	bool selectedSmoother() const { return best_smoother_; }
	int selectedMAWindow() const { return best_ma_window_; }
	bool selectedSeasonalPeriod() const { return best_seasonal_period_; }
	double selectedCV_MAE() const { return best_cv_mae_; }

	// Diagnostics
	struct OptimizationDiagnostics {
		int configs_evaluated = 0;
		double best_cv_mae = 0.0;
		bool best_seasonality_weights = false;
		bool best_smoother = false;
		int best_ma_window = 7;
		bool best_seasonal_period = true;
		double optimization_time_ms = 0.0;
	};

	const OptimizationDiagnostics& diagnostics() const {
		return diagnostics_;
	}

private:
	Config config_;

	// Selected parameters (statsforecast grid)
	bool best_seasonality_weights_ = false;
	bool best_smoother_ = false;
	int best_ma_window_ = 5;
	bool best_seasonal_period_ = true;
	double best_cv_mae_ = std::numeric_limits<double>::infinity();

	// Fitted model
	std::unique_ptr<MFLES> fitted_model_;
	OptimizationDiagnostics diagnostics_;

	// Optimization
	struct CandidateConfig {
		bool seasonality_weights;
		bool smoother;
		int ma_window;
		bool seasonal_period;
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
