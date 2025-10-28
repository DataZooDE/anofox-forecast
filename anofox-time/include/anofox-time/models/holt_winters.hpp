#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/ets.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <memory>
#include <string>

namespace anofoxtime::models {

/**
 * @brief Holt-Winters Seasonal Method
 * 
 * Simplified wrapper around ETS for the classic Holt-Winters method.
 * Provides an easier API than manually configuring ETS.
 * 
 * Two variants:
 *   - Additive:       ETS(A,A,A) - for constant seasonal variation
 *   - Multiplicative: ETS(A,A,M) - for proportional seasonal variation
 */
class HoltWinters : public IForecaster {
public:
	enum class SeasonType {
		Additive,       // ETS(A,A,A)
		Multiplicative  // ETS(A,A,M)
	};
	
	/**
	 * @brief Construct a Holt-Winters forecaster
	 * @param seasonal_period Seasonal period (e.g., 12 for monthly)
	 * @param season_type Additive or Multiplicative seasonality
	 * @param alpha Level smoothing parameter (default: 0.2)
	 * @param beta Trend smoothing parameter (default: 0.1)
	 * @param gamma Seasonal smoothing parameter (default: 0.1)
	 */
	HoltWinters(int seasonal_period, SeasonType season_type,
	            double alpha = 0.2, double beta = 0.1, double gamma = 0.1);
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	
	std::string getName() const override {
		return "HoltWinters";
	}
	
	// Accessors
	int seasonalPeriod() const {
		return seasonal_period_;
	}
	
	SeasonType seasonType() const {
		return season_type_;
	}
	
	const std::vector<double>& fittedValues() const;
	const std::vector<double>& residuals() const;
	
private:
	int seasonal_period_;
	SeasonType season_type_;
	std::unique_ptr<ETS> ets_model_;
	bool is_fitted_ = false;
};

} // namespace anofoxtime::models

