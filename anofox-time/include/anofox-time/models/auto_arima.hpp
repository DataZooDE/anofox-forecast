#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/arima.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/core/forecast.hpp"
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace anofoxtime::models {

/**
 * @brief Model orders selected by AutoARIMA.
 */
struct AutoARIMAComponents {
	int p = 0;  // Non-seasonal AR order
	int d = 0;  // Non-seasonal differencing order
	int q = 0;  // Non-seasonal MA order
	int P = 0;  // Seasonal AR order
	int D = 0;  // Seasonal differencing order
	int Q = 0;  // Seasonal MA order
	int seasonal_period = 0;  // Seasonal period (0 = non-seasonal)
	bool include_drift = false;
	bool include_constant = false;
};

/**
 * @brief Coefficients from the fitted AutoARIMA model.
 */
struct AutoARIMAParameters {
	std::vector<double> ar_coefficients;
	std::vector<double> ma_coefficients;
	std::vector<double> seasonal_ar_coefficients;
	std::vector<double> seasonal_ma_coefficients;
	double intercept = 0.0;
	double drift = 0.0;
};

/**
 * @brief Model quality diagnostics for AutoARIMA.
 */
struct AutoARIMAMetrics {
	double log_likelihood = std::numeric_limits<double>::quiet_NaN();
	double aic = std::numeric_limits<double>::quiet_NaN();
	double aicc = std::numeric_limits<double>::quiet_NaN();
	double bic = std::numeric_limits<double>::quiet_NaN();
	double sigma2 = std::numeric_limits<double>::quiet_NaN();
};

/**
 * @brief Diagnostic information about the AutoARIMA fitting process.
 */
struct AutoARIMADiagnostics {
	int models_evaluated = 0;
	int models_failed = 0;
	std::size_t training_data_size = 0;
	bool stepwise_used = true;
};

/**
 * @brief Automatic ARIMA model selection with seasonal support.
 *
 * The class performs stepwise search (or exhaustive if configured) to find
 * the best ARIMA(p,d,q)(P,D,Q)[s] model based on information criterion (AICc by default).
 * It follows the same design pattern as AutoETS.
 */
class AutoARIMA : public IForecaster {
public:
	/**
	 * @brief Construct AutoARIMA with optional seasonal period.
	 * @param seasonal_period Seasonal period (e.g., 12 for monthly data, 0 for non-seasonal)
	 */
	explicit AutoARIMA(int seasonal_period = 0);

	enum class InformationCriterion {
		AIC,
		AICc,
		BIC
	};

	struct CandidateConfig {
		int p = 0;
		int d = 0;
		int q = 0;
		int P = 0;
		int D = 0;
		int Q = 0;
		bool include_drift = false;
		bool include_constant = false;
	};

	void fit(const core::TimeSeries &ts) override;
	core::Forecast predict(int horizon) override;
	core::Forecast predictWithConfidence(int horizon, double confidence);

	// Configuration methods (method chaining)
	AutoARIMA &setMaxP(int max_p);
	AutoARIMA &setMaxD(int max_d);
	AutoARIMA &setMaxQ(int max_q);
	AutoARIMA &setMaxSeasonalP(int max_P);
	AutoARIMA &setMaxSeasonalD(int max_D);
	AutoARIMA &setMaxSeasonalQ(int max_Q);
	AutoARIMA &setStartP(int start_p);
	AutoARIMA &setStartQ(int start_q);
	AutoARIMA &setStartSeasonalP(int start_P);
	AutoARIMA &setStartSeasonalQ(int start_Q);
	AutoARIMA &setStepwise(bool stepwise);
	AutoARIMA &setInformationCriterion(InformationCriterion ic);
	AutoARIMA &setAllowDrift(bool allow_drift);
	AutoARIMA &setAllowMeanTerm(bool allow_mean);
	AutoARIMA &setSeasonalTest(bool test_seasonal);
	AutoARIMA &setApproximation(bool use_approximation);
	AutoARIMA &setMaxIterations(int max_iter);

	std::string getName() const override {
		return "AutoARIMA";
	}

	const AutoARIMAComponents &components() const;
	const AutoARIMAParameters &parameters() const;
	const AutoARIMAMetrics &metrics() const;
	const AutoARIMADiagnostics &diagnostics() const;
	const std::vector<double> &fittedValues() const;
	const std::vector<double> &residuals() const;

private:
	int seasonal_period_;
	int max_p_ = 5;
	int max_d_ = 2;
	int max_q_ = 5;
	int max_P_ = 2;
	int max_D_ = 1;
	int max_Q_ = 2;
	int start_p_ = 2;
	int start_q_ = 2;
	int start_P_ = 1;
	int start_Q_ = 1;
	bool stepwise_ = true;
	InformationCriterion ic_ = InformationCriterion::AICc;
	bool allow_drift_ = false;
	bool allow_mean_ = true;
	bool test_seasonal_ = true;
	bool approximation_ = false;
	int max_iterations_ = 100;

	AutoARIMAComponents components_;
	AutoARIMAParameters parameters_;
	AutoARIMAMetrics metrics_;
	AutoARIMADiagnostics diagnostics_;
	std::vector<double> fitted_;
	std::vector<double> residuals_;
	std::unique_ptr<ARIMA> fitted_model_;
	bool is_fitted_ = false;

	class CandidateEvaluator;
	struct CandidateResult {
		bool valid = false;
		CandidateConfig config;
		AutoARIMAMetrics metrics;
		std::unique_ptr<ARIMA> model;
	};

	void ensureUnivariate(const core::TimeSeries &ts) const;
	int determineDifferencing(const std::vector<double> &data, int max_d) const;
	int determineSeasonalDifferencing(const std::vector<double> &data, int seasonal_period, int max_D) const;
	std::vector<CandidateConfig> generateStepwiseCandidates(int d, int D) const;
	std::vector<CandidateConfig> generateExhaustiveCandidates(int d, int D) const;
	CandidateConfig findBestNeighbor(const CandidateConfig &current,
	                                  const std::vector<double> &data,
	                                  const CandidateEvaluator &evaluator) const;
};

} // namespace anofoxtime::models

