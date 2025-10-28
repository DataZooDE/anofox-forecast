#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/mstl_forecaster.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <vector>
#include <string>
#include <memory>
#include <limits>

namespace anofoxtime::models {

/**
 * @brief AutoMSTL - Automatic MSTL model selection
 * 
 * Automatically selects the best MSTL configuration by testing all combinations
 * of trend and seasonal forecasting methods and choosing the one with the lowest AIC.
 * 
 * Process:
 * 1. Generate candidate configurations (18 total):
 *    - 6 trend methods: Linear, SES, Holt, None, AutoETSAdditive, AutoETSMultiplicative
 *    - 3 seasonal methods: Cyclic, AutoETSAdditive, AutoETSMultiplicative
 * 2. Fit each candidate model
 * 3. Compute AIC for each model
 * 4. Select the model with the lowest AIC
 * 
 * Example usage:
 * @code
 *   AutoMSTL auto_mstl({12});  // Monthly seasonality
 *   auto_mstl.fit(time_series);
 *   auto forecast = auto_mstl.predict(12);
 *   
 *   std::cout << "Best trend: " << auto_mstl.selectedTrendMethod() << "\n";
 *   std::cout << "Best seasonal: " << auto_mstl.selectedSeasonalMethod() << "\n";
 *   std::cout << "AIC: " << auto_mstl.selectedAIC() << "\n";
 * @endcode
 */
class AutoMSTL : public IForecaster {
public:
	/**
	 * @brief Construct an AutoMSTL forecaster
	 * @param seasonal_periods Vector of seasonal periods (e.g., {12} for monthly data)
	 * @param mstl_iterations Number of MSTL decomposition iterations (default: 2)
	 * @param robust Use robust LOESS fitting (default: false)
	 */
	explicit AutoMSTL(
		std::vector<int> seasonal_periods,
		int mstl_iterations = 2,
		bool robust = false
	);
	
	void fit(const core::TimeSeries& ts) override;
	core::Forecast predict(int horizon) override;
	
	std::string getName() const override {
		return "AutoMSTL";
	}
	
	/**
	 * @brief Get the selected best model
	 */
	const MSTLForecaster& selectedModel() const {
		if (!is_fitted_) {
			throw std::runtime_error("AutoMSTL: Must call fit() before accessing selected model");
		}
		return *best_model_;
	}
	
	/**
	 * @brief Get the selected trend method
	 */
	MSTLForecaster::TrendMethod selectedTrendMethod() const {
		return selectedModel().trendMethod();
	}
	
	/**
	 * @brief Get the selected seasonal method
	 */
	MSTLForecaster::SeasonalMethod selectedSeasonalMethod() const {
		return selectedModel().seasonalMethod();
	}
	
	/**
	 * @brief Get the AIC of the selected model
	 */
	double selectedAIC() const {
		if (!is_fitted_) {
			throw std::runtime_error("AutoMSTL: Must call fit() before accessing AIC");
		}
		return best_aic_;
	}
	
	/**
	 * @brief Diagnostic information about the optimization process
	 */
	struct Diagnostics {
		int models_evaluated = 0;
		double best_aic = std::numeric_limits<double>::infinity();
		MSTLForecaster::TrendMethod best_trend;
		MSTLForecaster::SeasonalMethod best_seasonal;
		double optimization_time_ms = 0.0;
	};
	
	const Diagnostics& diagnostics() const {
		return diagnostics_;
	}

private:
	// Configuration
	std::vector<int> seasonal_periods_;
	int mstl_iterations_;
	bool robust_;
	
	// Selected model
	std::unique_ptr<MSTLForecaster> best_model_;
	double best_aic_ = std::numeric_limits<double>::infinity();
	
	// Diagnostics
	Diagnostics diagnostics_;
	
	// State
	bool is_fitted_ = false;
	
	/**
	 * @brief Candidate configuration for grid search
	 */
	struct Candidate {
		MSTLForecaster::TrendMethod trend;
		MSTLForecaster::SeasonalMethod seasonal;
	};
	
	/**
	 * @brief Generate all candidate configurations
	 */
	std::vector<Candidate> generateCandidates();
	
	/**
	 * @brief Compute AIC for a fitted model
	 * @param model The fitted MSTL model
	 * @param n Number of observations
	 * @return AIC value
	 */
	double computeAIC(const MSTLForecaster& model, int n);
	
	/**
	 * @brief Optimize parameters by testing all candidates
	 */
	void optimizeParameters(const core::TimeSeries& ts);
};

/**
 * @brief Builder for AutoMSTL forecaster
 */
class AutoMSTLBuilder {
public:
	AutoMSTLBuilder() = default;
	
	AutoMSTLBuilder& withSeasonalPeriods(std::vector<int> periods) {
		seasonal_periods_ = std::move(periods);
		return *this;
	}
	
	AutoMSTLBuilder& withMSTLIterations(int iterations) {
		mstl_iterations_ = iterations;
		return *this;
	}
	
	AutoMSTLBuilder& withRobust(bool robust) {
		robust_ = robust;
		return *this;
	}
	
	std::unique_ptr<AutoMSTL> build() {
		return std::make_unique<AutoMSTL>(
			seasonal_periods_,
			mstl_iterations_,
			robust_
		);
	}

private:
	std::vector<int> seasonal_periods_ = {12};
	int mstl_iterations_ = 2;
	bool robust_ = false;
};

} // namespace anofoxtime::models

