#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <optional>

namespace anofoxtime::models {

/**
 * @brief Trend fitting method options
 */
enum class TrendMethod {
	OLS,              // Ordinary Least Squares (fast, default)
	SIEGEL_ROBUST,    // Siegel Repeated Medians (robust to outliers)
	PIECEWISE         // Piecewise linear with changepoint detection (LASSO-based)
};

/**
 * @brief MFLES Enhanced - Full feature parity with statsforecast MFLES
 *
 * MFLES uses gradient boosted time series decomposition with 5 components:
 * 1. Median Component (optional, per-period or global baseline)
 * 2. Linear/Piecewise Trend (OLS, Siegel Robust, or Piecewise LASSO)
 * 3. Fourier Seasonality (multiple periods, weighted or unweighted)
 * 4. Residual Smoothing (ES ensemble or moving average)
 * 5. Exogenous Variables (future: external regressors)
 *
 * Key features matching statsforecast:
 * - Multiplicative decomposition (automatic log transform)
 * - Robust trend fitting (Siegel regression, changepoint detection)
 * - Weighted seasonality (increasing importance over time)
 * - ES ensemble (averages multiple alpha values)
 * - Outlier handling and capping
 * - Data normalization (log or z-score)
 * - Cross-validation based optimization
 *
 * Reference: statsforecast MFLES (https://github.com/Nixtla/statsforecast)
 */
class MFLES : public IForecaster {
public:
	/**
	 * @brief Construction parameters for MFLES
	 */
	struct Params {
		// Default constructor
		Params() = default;

		// Seasonal periods
		std::vector<int> seasonal_periods = {12};

		// Boosting configuration
		int max_rounds = 50;              // Maximum boosting iterations (statsforecast default)
		double convergence_threshold = 0.01;  // Early stopping threshold

		// Learning rates (match statsforecast defaults)
		double lr_median = 1.0;           // Median component learning rate
		double lr_trend = 0.9;            // Trend component learning rate (linear_lr)
		double lr_season = 0.9;           // Seasonal component learning rate (seasonal_lr)
		double lr_rs = 1.0;               // Residual smoothing learning rate (rs_lr)
		double lr_exogenous = 1.0;        // Exogenous learning rate (future)

		// Decomposition mode
		bool multiplicative = false;      // Auto-detect based on CoV if nullopt
		std::optional<bool> multiplicative_override;  // User override
		double cov_threshold = 0.7;       // CoV threshold for auto-detection

		// Trend configuration
		TrendMethod trend_method = TrendMethod::OLS;
		bool trend_penalty = true;        // Apply R²-based penalty to extrapolation
		bool changepoints = true;         // Enable changepoint detection (if PIECEWISE)
		double n_changepoints_pct = 0.25; // 25% of series length
		double lasso_alpha = 1.0;         // LASSO L1 penalty
		double decay = -1.0;              // Adaptive decay (-1 = auto)

		// Seasonality configuration
		int fourier_order = -1;           // -1 = adaptive (5/10/15 based on period)
		bool seasonality_weights = false; // Time-varying seasonal importance

		// Residual smoothing configuration
		bool smoother = false;            // false = ES ensemble, true = MA
		int ma_window = 5;                // Moving average window (if smoother=true)
		double min_alpha = 0.05;          // ES ensemble min alpha
		double max_alpha = 1.0;           // ES ensemble max alpha
		int es_ensemble_steps = 20;      // Number of alphas to test

		// Median component
		bool moving_medians = false;      // Per-period medians vs global

		// Outlier handling
		bool cap_outliers = true;         // Cap extreme values
		double outlier_sigma = 3.0;       // Threshold (mean ± N*std)
		int outlier_cap_start_round = 5;  // Start capping after N rounds

		// Round penalty (fine-grained iteration control)
		double round_penalty = 0.0001;
	};

	/**
	 * @brief Decomposition components (for seasonal_decompose())
	 */
	struct Decomposition {
		std::vector<double> trend;
		std::vector<double> seasonal;
		std::vector<double> level;
		std::vector<double> residuals;
		std::optional<std::vector<double>> exogenous;  // future
	};

	/**
	 * @brief Construct an MFLES forecaster with default parameters
	 */
	MFLES();

	/**
	 * @brief Construct an MFLES forecaster
	 * @param params Configuration parameters
	 */
	explicit MFLES(const Params& params);

	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;

	std::string getName() const override {
		return "MFLES_Enhanced";
	}

	/**
	 * @brief Decompose time series into components
	 * @return Decomposition structure with trend, seasonal, level, residuals
	 */
	Decomposition seasonal_decompose() const;

	// Accessors
	const std::vector<double>& fittedValues() const { return fitted_; }
	const std::vector<double>& residuals() const { return residuals_; }
	const Params& parameters() const { return params_; }
	bool isMultiplicative() const { return is_multiplicative_; }
	int actualRoundsUsed() const { return actual_rounds_; }

	/**
	 * @brief Configuration presets for common use cases
	 */

	// Fast preset - Quick forecasting with minimal computation
	static Params fastPreset() {
		Params params;
		params.max_rounds = 3;
		params.fourier_order = 3;
		params.trend_method = TrendMethod::OLS;
		params.es_ensemble_steps = 10;
		params.cap_outliers = false;
		return params;
	}

	// Balanced preset - Recommended default configuration
	static Params balancedPreset() {
		Params params;
		params.max_rounds = 5;
		params.fourier_order = 5;
		params.trend_method = TrendMethod::OLS;
		params.es_ensemble_steps = 20;
		params.cap_outliers = true;
		return params;
	}

	// Accurate preset - High accuracy with more computation
	static Params accuratePreset() {
		Params params;
		params.max_rounds = 10;
		params.fourier_order = 7;
		params.trend_method = TrendMethod::SIEGEL_ROBUST;
		params.es_ensemble_steps = 30;
		params.seasonality_weights = true;
		params.cap_outliers = true;
		return params;
	}

	// Robust preset - Maximum resistance to outliers
	static Params robustPreset() {
		Params params;
		params.max_rounds = 7;
		params.fourier_order = 5;
		params.trend_method = TrendMethod::SIEGEL_ROBUST;
		params.es_ensemble_steps = 20;
		params.seasonality_weights = true;
		params.cap_outliers = true;
		params.outlier_sigma = 2.5;
		return params;
	}

private:
	// Configuration
	Params params_;

	// Preprocessing state
	bool is_multiplicative_ = false;
	double mean_ = 0.0;          // For z-score normalization
	double std_ = 1.0;           // For z-score normalization
	double const_offset_ = 0.0;  // For multiplicative mode (min value)
	std::vector<double> original_data_;
	std::vector<double> preprocessed_data_;

	// Fitted components (accumulated over iterations)
	std::vector<double> median_component_;  // Phase 8 Fix #1: Changed from scalar to vector
	std::vector<double> trend_component_;
	std::map<int, std::vector<double>> seasonal_components_;
	std::vector<double> level_component_;

	// Fourier coefficients for forecasting (per period)
	struct FourierCoeffs {
		std::vector<double> sin_coeffs;
		std::vector<double> cos_coeffs;
		int K;  // Number of Fourier pairs
	};
	std::map<int, FourierCoeffs> fourier_coeffs_;

	// Trend parameters
	double trend_slope_ = 0.0;
	double trend_intercept_ = 0.0;
	std::vector<double> accumulated_trend_;  // Last 2 fitted trend values for forecasting (size 2)
	std::vector<double> changepoint_coefs_;  // For piecewise trend
	std::vector<int> changepoint_indices_;

	// ES ensemble/MA parameters
	double final_level_ = 0.0;   // Final ES level for forecasting
	std::vector<double> es_ensemble_alphas_;  // Alpha values used in ensemble

	// Data and diagnostics
	std::vector<double> fitted_;
	std::vector<double> residuals_;
	bool is_fitted_ = false;
	int actual_rounds_ = 0;

	// Preprocessing methods
	void preprocess(const std::vector<double>& data);
	void postprocess(std::vector<double>& forecasts) const;
	bool shouldUseMultiplicative(const std::vector<double>& data) const;
	double computeCoV(const std::vector<double>& data) const;

	// Component fitting methods
	std::vector<double> fitMedianComponent(const std::vector<double>& data);  // Phase 8 Fix #1: Returns vector instead of scalar
	std::vector<double> fitLinearTrend(const std::vector<double>& data);
	std::vector<double> fitSiegelTrend(const std::vector<double>& data);
	std::vector<double> fitPiecewiseTrend(const std::vector<double>& data);
	std::vector<double> fitFourierSeason(const std::vector<double>& data, int period, bool weighted = false);
	std::vector<double> fitESEnsemble(const std::vector<double>& data);
	std::vector<double> fitMovingAverage(const std::vector<double>& data, int window);

	// Outlier handling
	void capOutliers(std::vector<double>& data) const;
	std::vector<bool> detectOutliers(const std::vector<double>& data) const;

	// Fourier helpers
	int optimalK(int period) const;
	int adaptiveK(int period) const;  // statsforecast adaptive logic
	std::vector<double> projectFourier(int period, int horizon, int start_index = 0) const;
	std::vector<double> getSeasonalityWeights(int n, int period) const;

	// Forecast helpers
	std::vector<double> projectTrend(int horizon, int start_index = 0) const;
	std::vector<double> projectLevel(int horizon) const;
	void computeFittedValues();
	double computeTrendPenalty() const;  // R²-based penalty

	// Diagnostics
	double computeRSquared(const std::vector<double>& actual, const std::vector<double>& fitted) const;
};

/**
 * @brief Builder for MFLES forecaster (fluent API)
 */
class MFLESBuilder {
public:
	MFLESBuilder() = default;

	MFLESBuilder& withSeasonalPeriods(std::vector<int> periods) {
		params_.seasonal_periods = std::move(periods);
		return *this;
	}

	MFLESBuilder& withMaxRounds(int rounds) {
		params_.max_rounds = rounds;
		return *this;
	}

	MFLESBuilder& withLearningRates(double trend, double season, double rs) {
		params_.lr_trend = trend;
		params_.lr_season = season;
		params_.lr_rs = rs;
		return *this;
	}

	MFLESBuilder& withMultiplicative(bool enable) {
		params_.multiplicative_override = enable;
		return *this;
	}

	MFLESBuilder& withTrendMethod(TrendMethod method) {
		params_.trend_method = method;
		return *this;
	}

	MFLESBuilder& withChangepoints(bool enable, double pct = 0.25) {
		params_.changepoints = enable;
		params_.n_changepoints_pct = pct;
		return *this;
	}

	MFLESBuilder& withSeasonalityWeights(bool enable) {
		params_.seasonality_weights = enable;
		return *this;
	}

	MFLESBuilder& withESEnsemble(double min_alpha, double max_alpha, int steps = 20) {
		params_.smoother = false;
		params_.min_alpha = min_alpha;
		params_.max_alpha = max_alpha;
		params_.es_ensemble_steps = steps;
		return *this;
	}

	MFLESBuilder& withMovingAverage(int window) {
		params_.smoother = true;
		params_.ma_window = window;
		return *this;
	}

	MFLESBuilder& withFourierOrder(int order) {
		params_.fourier_order = order;
		return *this;
	}

	MFLESBuilder& withOutlierCapping(bool enable, double sigma = 3.0) {
		params_.cap_outliers = enable;
		params_.outlier_sigma = sigma;
		return *this;
	}

	std::unique_ptr<MFLES> build() {
		return std::make_unique<MFLES>(params_);
	}

private:
	MFLES::Params params_;
};

} // namespace anofoxtime::models
