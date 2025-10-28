#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/tbats.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>
#include <memory>
#include <limits>

namespace anofoxtime::models {

/**
 * @brief AutoTBATS - Automatic parameter optimization for TBATS
 * 
 * AutoTBATS automatically tests all feasible combinations of TBATS parameters
 * and selects the model with the lowest AIC. It optimizes:
 * - Box-Cox transformation (with different lambda values)
 * - Trend configuration (none/linear/damped)
 * - ARMA error orders
 * - Fourier terms (automatic selection per period)
 * 
 * Reference: De Livera, Hyndman, & Snyder (2011)
 */
class AutoTBATS : public IForecaster {
public:
	/**
	 * @brief Construct an AutoTBATS forecaster
	 * @param seasonal_periods Vector of seasonal periods
	 */
	explicit AutoTBATS(std::vector<int> seasonal_periods);
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	
	std::string getName() const override {
		return "AutoTBATS";
	}
	
	// Access selected model
	const TBATS& selectedModel() const {
		if (!fitted_model_) {
			throw std::runtime_error("AutoTBATS: Must call fit() before accessing selected model");
		}
		return *fitted_model_;
	}
	
	const TBATS::Config& selectedConfig() const {
		if (!fitted_model_) {
			throw std::runtime_error("AutoTBATS: Must call fit() before accessing config");
		}
		return fitted_model_->config();
	}
	
	double selectedAIC() const {
		if (!fitted_model_) {
			throw std::runtime_error("AutoTBATS: Must call fit() before accessing AIC");
		}
		return fitted_model_->aic();
	}
	
	// Diagnostics
	struct Diagnostics {
		int models_evaluated = 0;
		double best_aic = std::numeric_limits<double>::infinity();
		TBATS::Config best_config;
		double optimization_time_ms = 0.0;
	};
	
	const Diagnostics& diagnostics() const {
		return diagnostics_;
	}

private:
	std::vector<int> seasonal_periods_;
	std::unique_ptr<TBATS> fitted_model_;
	Diagnostics diagnostics_;
	
	std::vector<TBATS::Config> generateCandidates();
	double computeAIC(const TBATS& model);
	void optimizeParameters(const core::TimeSeries& ts);
};

/**
 * @brief Builder for AutoTBATS forecaster
 */
class AutoTBATSBuilder {
public:
	AutoTBATSBuilder() = default;
	
	AutoTBATSBuilder& withSeasonalPeriods(std::vector<int> periods) {
		seasonal_periods_ = std::move(periods);
		return *this;
	}
	
	std::unique_ptr<AutoTBATS> build() {
		return std::make_unique<AutoTBATS>(seasonal_periods_);
	}

private:
	std::vector<int> seasonal_periods_ = {12};
};

} // namespace anofoxtime::models

