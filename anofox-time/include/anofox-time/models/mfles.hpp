#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>
#include <map>
#include <memory>

namespace anofoxtime::models {

/**
 * @brief MFLES - Multiple seasonalities Fourier-based exponential smoothing
 * 
 * MFLES uses gradient boosted time series decomposition to model complex patterns.
 * It iteratively fits three components on residuals:
 * 1. Linear Trend (learning rate: lr_trend)
 * 2. Fourier Seasonality for multiple periods (learning rate: lr_season)
 * 3. Exponential Smoothing Level (learning rate: lr_level)
 * 
 * The method uses Fourier term pairs (sin/cos) to capture seasonal patterns,
 * making it particularly effective for data with multiple seasonalities.
 * 
 * Reference: Inspired by gradient boosted decomposition approaches
 */
class MFLES : public IForecaster {
public:
	/**
	 * @brief Construct an MFLES forecaster
	 * @param seasonal_periods Vector of seasonal periods (e.g., {12} for monthly, {7, 365} for daily with weekly+yearly)
	 * @param n_iterations Number of gradient boosting iterations (default: 10, tuned for best accuracy)
	 * @param lr_trend Learning rate for trend component (default: 0.3, conservative for stability)
	 * @param lr_season Learning rate for seasonal components (default: 0.5, balanced)
	 * @param lr_level Learning rate for level component (default: 0.8, high for residual capture)
	 */
	explicit MFLES(std::vector<int> seasonal_periods = {12}, 
	               int n_iterations = 10,
	               double lr_trend = 0.3,
	               double lr_season = 0.5,
	               double lr_level = 0.8);
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	
	std::string getName() const override {
		return "MFLES";
	}
	
	// Accessors
	const std::vector<double>& fittedValues() const {
		return fitted_;
	}
	
	const std::vector<double>& residuals() const {
		return residuals_;
	}
	
	const std::vector<int>& seasonalPeriods() const {
		return seasonal_periods_;
	}
	
	int iterations() const {
		return n_iterations_;
	}
	
	double trendLearningRate() const {
		return lr_trend_;
	}
	
	double seasonalLearningRate() const {
		return lr_season_;
	}
	
	double levelLearningRate() const {
		return lr_level_;
	}

private:
	// Configuration
	std::vector<int> seasonal_periods_;
	int n_iterations_;
	double lr_trend_;
	double lr_season_;
	double lr_level_;
	
	// Fitted components (accumulated over iterations)
	std::vector<double> trend_component_;       // Accumulated trend
	std::map<int, std::vector<double>> seasonal_components_; // Per period
	std::vector<double> level_component_;       // ES level
	
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
	
	// ES level
	double es_level_ = 0.0;
	double es_alpha_ = 0.3;
	
	// Data and diagnostics
	std::vector<double> history_;
	std::vector<double> fitted_;
	std::vector<double> residuals_;
	bool is_fitted_ = false;
	
	// Component fitting methods
	std::vector<double> fitLinearTrend(const std::vector<double>& data);
	std::vector<double> fitFourierSeason(const std::vector<double>& data, int period);
	std::vector<double> fitESLevel(const std::vector<double>& data);
	
	// Fourier helpers
	int optimalK(int period) const;
	std::vector<double> projectFourier(int period, int horizon, int start_index = 0);
	
	// Forecast helpers
	std::vector<double> projectTrend(int horizon, int start_index = 0);
	std::vector<double> projectLevel(int horizon);
	void computeFittedValues();
};

/**
 * @brief Builder for MFLES forecaster
 */
class MFLESBuilder {
public:
	MFLESBuilder() = default;
	
	MFLESBuilder& withSeasonalPeriods(std::vector<int> periods) {
		seasonal_periods_ = std::move(periods);
		return *this;
	}
	
	MFLESBuilder& withIterations(int n) {
		n_iterations_ = n;
		return *this;
	}
	
	MFLESBuilder& withTrendLearningRate(double lr) {
		lr_trend_ = lr;
		return *this;
	}
	
	MFLESBuilder& withSeasonalLearningRate(double lr) {
		lr_season_ = lr;
		return *this;
	}
	
	MFLESBuilder& withLevelLearningRate(double lr) {
		lr_level_ = lr;
		return *this;
	}
	
	std::unique_ptr<MFLES> build() {
		return std::make_unique<MFLES>(seasonal_periods_, n_iterations_, 
		                                lr_trend_, lr_season_, lr_level_);
	}

private:
	std::vector<int> seasonal_periods_ = {12};
	int n_iterations_ = 10;   // Tuned for best accuracy/speed tradeoff
	double lr_trend_ = 0.3;   // Conservative for stability
	double lr_season_ = 0.5;  // Balanced
	double lr_level_ = 0.8;   // High for residual capture
};

} // namespace anofoxtime::models

