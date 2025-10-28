#pragma once

#include "anofox-time/models/iforecaster.hpp"
#include "anofox-time/models/ets.hpp"
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
 * @brief Selected error component for the fitted AutoETS model.
 */
enum class AutoETSErrorType {
	Additive,
	Multiplicative
};

/**
 * @brief Selected trend component for the fitted AutoETS model.
 *
 * The `damped` flag on AutoETS indicates whether the chosen trend is damped.
 */
enum class AutoETSTrendType {
	None,
	Additive,
	Multiplicative
};

/**
 * @brief Selected seasonal component for the fitted AutoETS model.
 */
enum class AutoETSSeasonType {
	None,
	Additive,
	Multiplicative
};

/**
 * @brief Metadata describing the model selected by AutoETS.
 */
struct AutoETSComponents {
	AutoETSErrorType error = AutoETSErrorType::Additive;
	AutoETSTrendType trend = AutoETSTrendType::None;
	AutoETSSeasonType season = AutoETSSeasonType::None;
	bool damped = false;
	std::size_t season_length = 1;
};

/**
 * @brief Smoothing parameters returned by AutoETS.
 */
struct AutoETSParameters {
	double alpha = std::numeric_limits<double>::quiet_NaN();
	double beta = std::numeric_limits<double>::quiet_NaN();
	double gamma = std::numeric_limits<double>::quiet_NaN();
	double phi = std::numeric_limits<double>::quiet_NaN();
};

/**
 * @brief Model quality diagnostics mirroring augurs AutoETS outputs.
 */
struct AutoETSMetrics {
	double log_likelihood = std::numeric_limits<double>::quiet_NaN();
	double aic = std::numeric_limits<double>::quiet_NaN();
	double aicc = std::numeric_limits<double>::quiet_NaN();
	double bic = std::numeric_limits<double>::quiet_NaN();
	double mse = std::numeric_limits<double>::quiet_NaN();
	double amse = std::numeric_limits<double>::quiet_NaN();
	double sigma = std::numeric_limits<double>::quiet_NaN();
};

struct AutoETSDiagnostics {
	bool optimizer_converged = false;
	int optimizer_iterations = 0;
	double optimizer_objective = std::numeric_limits<double>::quiet_NaN();
	std::size_t training_data_size = 0;
};

/**
 * @brief Thin C++ wrapper around the augurs AutoETS implementation.
 *
 * The class delegates fitting and forecasting to the Rust reference, ensuring
 * parity with the original implementation while presenting the standard
 * `IForecaster` interface within the C++ codebase.
 */
class AutoETS : public IForecaster {
public:
	AutoETS(int season_length, std::string spec);

	enum class DampedPolicy {
		Auto,
		Always,
		Never
	};

	enum class OptimizationCriterion {
		Likelihood,
		MSE,
		AMSE,
		Sigma
	};

	struct Spec {
		std::vector<ETSErrorType> errors;
		std::vector<ETSTrendType> trends;
		std::vector<ETSSeasonType> seasons;
	};

	struct CandidateConfig {
		ETSErrorType error;
		ETSTrendType trend;
		ETSSeasonType season;
		bool damped;
	};

	void fit(const core::TimeSeries &ts) override;
	core::Forecast predict(int horizon) override;

	AutoETS &setAllowMultiplicativeTrend(bool allow);
	AutoETS &setDampedPolicy(DampedPolicy policy);
	AutoETS &setOptimizationCriterion(OptimizationCriterion criterion);
	AutoETS &setNmse(std::size_t horizon);
	AutoETS &setMaxIterations(int iterations);
	AutoETS &setPinnedAlpha(double alpha);
	AutoETS &clearPinnedAlpha();
	AutoETS &setPinnedBeta(double beta);
	AutoETS &clearPinnedBeta();
	AutoETS &setPinnedGamma(double gamma);
	AutoETS &clearPinnedGamma();
	AutoETS &setPinnedPhi(double phi);
	AutoETS &clearPinnedPhi();

	std::string getName() const override {
		return "AutoETS";
	}

	const AutoETSComponents &components() const;
	const AutoETSParameters &parameters() const;
	const AutoETSMetrics &metrics() const;
	const AutoETSDiagnostics &diagnostics() const;
	const std::vector<double> &fittedValues() const;
	const std::vector<double> &residuals() const;

private:
	int season_length_ = 1;
	std::string spec_text_;
	Spec spec_;
	AutoETSComponents components_;
	AutoETSParameters parameters_;
	AutoETSMetrics metrics_;
	AutoETSDiagnostics diagnostics_;

	class CandidateEvaluator;
	struct CandidateResult {
		bool valid = false;
		ETSConfig config;
		AutoETSMetrics metrics;
		AutoETSParameters params;
		AutoETSComponents components;
		double level0 = 0.0;
		std::optional<double> trend0;
		bool has_state_override = false;
		int optimizer_iterations = 0;
		bool optimizer_converged = false;
		double optimizer_objective = std::numeric_limits<double>::quiet_NaN();
	};
	bool allow_multiplicative_trend_ = false;
	DampedPolicy damped_policy_ = DampedPolicy::Auto;
	OptimizationCriterion optimization_criterion_ = OptimizationCriterion::Likelihood;
	std::size_t nmse_horizon_ = 3;
	int max_iterations_ = 300; 
	bool trend_explicit_multiplicative_ = false;
	bool trend_auto_allows_multiplicative_ = false;
	std::optional<double> pinned_alpha_;
	std::optional<double> pinned_beta_;
	std::optional<double> pinned_gamma_;
	std::optional<double> pinned_phi_;
	std::unique_ptr<ETS> fitted_model_;
	std::vector<double> fitted_;
	std::vector<double> residuals_;
	bool is_fitted_ = false;

	void ensureUnivariate(const core::TimeSeries &ts) const;
	std::vector<CandidateConfig> enumerateCandidates(const std::vector<double> &values) const;
	std::vector<bool> dampedCandidates(ETSTrendType trend) const;
};

} // namespace anofoxtime::models
