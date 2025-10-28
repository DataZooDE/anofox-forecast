#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>
#include <map>
#include <memory>
#include <limits>

namespace anofoxtime::models {

/**
 * @brief TBATS - Trigonometric, Box-Cox, ARMA errors, Trend, Seasonal components
 * 
 * TBATS is an innovations state-space model for forecasting time series with
 * multiple seasonalities. It uses:
 * - Trigonometric (Fourier) representation for seasonal patterns
 * - Optional Box-Cox transformation for variance stabilization
 * - Optional ARMA errors for autocorrelation
 * - Trend component (with optional damping)
 * - Multiple seasonal components
 * 
 * Reference: De Livera, Hyndman, & Snyder (2011). "Forecasting time series
 * with complex seasonal patterns using exponential smoothing."
 */
class TBATS : public IForecaster {
public:
	/**
	 * @brief Configuration for TBATS model
	 */
	struct Config {
		std::vector<int> seasonal_periods;  // e.g., {24, 168} for hourly data
		
		// Box-Cox transformation
		bool use_box_cox = false;
		double box_cox_lambda = 1.0;  // 0 = log, 1 = no transform
		
		// Trend
		bool use_trend = true;
		bool use_damped_trend = false;
		double damping_param = 0.98;
		
		// ARMA errors
		int ar_order = 0;
		int ma_order = 0;
		
		// Fourier terms (auto-selected if empty)
		std::vector<int> fourier_k;  // K per period
		
		// Smoothing parameters (estimated if not set)
		double alpha = -1.0;  // Level
		double beta = -1.0;   // Trend
		std::vector<double> gamma;  // Seasonal (per period)
	};
	
	/**
	 * @brief Construct a TBATS forecaster
	 * @param config Configuration for the model
	 */
	explicit TBATS(const Config& config);
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	
	std::string getName() const override {
		return "TBATS";
	}
	
	// Accessors
	const Config& config() const { return config_; }
	
	const std::vector<double>& fittedValues() const {
		if (!is_fitted_) {
			throw std::runtime_error("TBATS: Must call fit() before accessing fitted values");
		}
		return fitted_;
	}
	
	const std::vector<double>& residuals() const {
		if (!is_fitted_) {
			throw std::runtime_error("TBATS: Must call fit() before accessing residuals");
		}
		return residuals_;
	}
	
	double aic() const {
		if (!is_fitted_) {
			throw std::runtime_error("TBATS: Must call fit() before accessing AIC");
		}
		return aic_;
	}

private:
	Config config_;
	bool is_fitted_ = false;
	
	// State variables
	double level_state_ = 0.0;
	double trend_state_ = 0.0;
	
	// Seasonal states: [period][fourier_pair_index][sin/cos]
	std::map<int, std::vector<std::pair<double, double>>> seasonal_states_;
	
	// ARMA error states
	std::vector<double> ar_states_;
	std::vector<double> ma_states_;
	
	// Data and diagnostics
	std::vector<double> history_;
	std::vector<double> transformed_history_;  // After Box-Cox
	std::vector<double> fitted_;
	std::vector<double> residuals_;
	double aic_ = std::numeric_limits<double>::infinity();
	
	// Estimated parameters
	double alpha_estimated_ = 0.1;
	double beta_estimated_ = 0.01;
	std::vector<double> gamma_estimated_;
	
	// Box-Cox transformation
	std::vector<double> applyBoxCox(const std::vector<double>& data);
	std::vector<double> inverseBoxCox(const std::vector<double>& data);
	double boxCoxTransform(double value);
	double inverseBoxCoxTransform(double value);
	
	// Fourier term selection
	int selectOptimalK(int period, int max_k = 10);
	
	// State-space fitting
	void initializeStates(const std::vector<double>& data);
	void fitStateSpace(const std::vector<double>& data);
	void updateStates(double observation, double& fitted_value, double& error);
	
	// Forecasting
	std::vector<double> forecastStateSpace(int horizon);
	
	// Helpers
	void computeAIC(const std::vector<double>& residuals, int num_params);
	void computeFittedValues();
};

/**
 * @brief Builder for TBATS forecaster
 */
class TBATSBuilder {
public:
	TBATSBuilder() = default;
	
	TBATSBuilder& withSeasonalPeriods(std::vector<int> periods) {
		config_.seasonal_periods = std::move(periods);
		return *this;
	}
	
	TBATSBuilder& withBoxCox(bool use, double lambda = 0.0) {
		config_.use_box_cox = use;
		config_.box_cox_lambda = lambda;
		return *this;
	}
	
	TBATSBuilder& withTrend(bool use) {
		config_.use_trend = use;
		return *this;
	}
	
	TBATSBuilder& withDampedTrend(bool use, double damping = 0.98) {
		config_.use_damped_trend = use;
		config_.damping_param = damping;
		return *this;
	}
	
	TBATSBuilder& withARMA(int ar_order, int ma_order) {
		config_.ar_order = ar_order;
		config_.ma_order = ma_order;
		return *this;
	}
	
	TBATSBuilder& withFourierK(std::vector<int> fourier_k) {
		config_.fourier_k = std::move(fourier_k);
		return *this;
	}
	
	std::unique_ptr<TBATS> build() {
		return std::make_unique<TBATS>(config_);
	}

private:
	TBATS::Config config_;
};

} // namespace anofoxtime::models

