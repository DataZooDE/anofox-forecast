#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/seasonality/mstl.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>
#include <memory>

namespace anofoxtime::models {

/**
 * @brief MSTL Forecaster - Multiple Seasonal-Trend decomposition with forecasting
 * 
 * MSTL combines Multiple Seasonal-Trend decomposition using LOESS (MSTL) with
 * trend forecasting to produce forecasts for time series with multiple seasonalities.
 * 
 * Process:
 * 1. Decompose time series into trend + multiple seasonal components + remainder
 * 2. Forecast the trend using selected method (linear, SES, Holt, or constant)
 * 3. Project seasonal components cyclically
 * 4. Combine trend forecast with seasonal projections
 * 
 * This is particularly useful for time series with multiple seasonal patterns,
 * such as hourly data with daily, weekly, and yearly seasonality.
 */
class MSTLForecaster : public IForecaster {
public:
	/**
	 * @brief Trend forecasting method selection
	 */
	enum class TrendMethod {
		Linear,                      // Linear regression (extrapolates trend)
		SES,                         // Simple exponential smoothing
		Holt,                        // Holt's linear trend method
		None,                        // Constant (uses last trend value)
		AutoETSTrendAdditive,        // AutoETS with additive trend, no season (ZAN)
		AutoETSTrendMultiplicative   // AutoETS with multiplicative trend, no season (ZMN)
	};
	
	/**
	 * @brief Seasonal forecasting method selection
	 */
	enum class SeasonalMethod {
		Cyclic,                  // Simple cyclic projection (existing behavior)
		AutoETSAdditive,         // AutoETS with additive season, no trend (ZNA)
		AutoETSMultiplicative    // AutoETS with multiplicative season, no trend (ZNM)
	};
	
	/**
	 * @brief Construct an MSTL forecaster
	 * @param seasonal_periods Vector of seasonal periods (e.g., {7, 365} for daily data)
	 * @param trend_method Method to forecast the trend component
	 * @param seasonal_method Method to forecast seasonal components
	 * @param mstl_iterations Number of MSTL decomposition iterations (default: 2)
	 * @param robust Use robust LOESS fitting (default: false)
	 */
	explicit MSTLForecaster(
		std::vector<int> seasonal_periods,
		TrendMethod trend_method = TrendMethod::Linear,
		SeasonalMethod seasonal_method = SeasonalMethod::Cyclic,
		int mstl_iterations = 2,
		bool robust = false
	);
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	
	std::string getName() const override {
		return "MSTL";
	}
	
	// Access decomposition components
	const seasonality::MSTLComponents& components() const {
		if (!is_fitted_) {
			throw std::runtime_error("MSTL: Must call fit() before accessing components");
		}
		return decomposition_->components();
	}
	
	const std::vector<int>& seasonalPeriods() const {
		return seasonal_periods_;
	}
	
	TrendMethod trendMethod() const {
		return trend_method_;
	}
	
	SeasonalMethod seasonalMethod() const {
		return seasonal_method_;
	}

private:
	// Configuration
	std::vector<int> seasonal_periods_;
	TrendMethod trend_method_;
	SeasonalMethod seasonal_method_;
	int mstl_iterations_;
	bool robust_;
	
	// Decomposition
	std::unique_ptr<seasonality::MSTLDecomposition> decomposition_;
	
	// Data
	std::vector<double> history_;
	bool is_fitted_ = false;
	
	// Forecasting methods - Trend
	std::vector<double> forecastTrendLinear(int horizon);
	std::vector<double> forecastTrendSES(int horizon);
	std::vector<double> forecastTrendHolt(int horizon);
	std::vector<double> forecastTrendNone(int horizon);
	std::vector<double> forecastTrendAutoETSAdditive(int horizon);
	std::vector<double> forecastTrendAutoETSMultiplicative(int horizon);
	
	// Forecasting methods - Deseasonalized (trend + remainder)
	std::vector<double> forecastDeseasonalized(int horizon);
	
	// Forecasting methods - Seasonal
	std::vector<double> projectSeasonalCyclic(
		const std::vector<double>& seasonal,
		int period,
		int horizon
	);
	std::vector<double> forecastSeasonalAutoETSAdditive(
		const std::vector<double>& seasonal,
		int period,
		int horizon
	);
	std::vector<double> forecastSeasonalAutoETSMultiplicative(
		const std::vector<double>& seasonal,
		int period,
		int horizon
	);
};

/**
 * @brief Builder for MSTL forecaster
 */
class MSTLForecasterBuilder {
public:
	MSTLForecasterBuilder() = default;
	
	MSTLForecasterBuilder& withSeasonalPeriods(std::vector<int> periods) {
		seasonal_periods_ = std::move(periods);
		return *this;
	}
	
	MSTLForecasterBuilder& withTrendMethod(MSTLForecaster::TrendMethod method) {
		trend_method_ = method;
		return *this;
	}
	
	MSTLForecasterBuilder& withSeasonalMethod(MSTLForecaster::SeasonalMethod method) {
		seasonal_method_ = method;
		return *this;
	}
	
	MSTLForecasterBuilder& withMSTLIterations(int iterations) {
		mstl_iterations_ = iterations;
		return *this;
	}
	
	MSTLForecasterBuilder& withRobust(bool robust) {
		robust_ = robust;
		return *this;
	}
	
	std::unique_ptr<MSTLForecaster> build() {
		return std::make_unique<MSTLForecaster>(
			seasonal_periods_,
			trend_method_,
			seasonal_method_,
			mstl_iterations_,
			robust_
		);
	}

private:
	std::vector<int> seasonal_periods_ = {12};
	MSTLForecaster::TrendMethod trend_method_ = MSTLForecaster::TrendMethod::Linear;
	MSTLForecaster::SeasonalMethod seasonal_method_ = MSTLForecaster::SeasonalMethod::Cyclic;
	int mstl_iterations_ = 2;
	bool robust_ = false;
};

} // namespace anofoxtime::models

