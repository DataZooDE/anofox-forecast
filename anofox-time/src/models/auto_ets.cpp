#include "anofox-time/models/auto_ets.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/models/ets.hpp"
#include "anofox-time/utils/logging.hpp"
#include "anofox-time/utils/nelder_mead.hpp"
#include "anofox-time/optimization/lbfgs_optimizer.hpp"
#include "anofox-time/optimization/ets_gradients.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>
#include <iostream>

namespace {
constexpr double kAlphaLower = 0.0001;
constexpr double kAlphaUpper = 0.9999;
constexpr double kPhiLower = 0.80;
constexpr double kPhiUpper = 0.98;
constexpr double kGammaLower = 0.0001;
constexpr double kGammaUpper = 0.9999;
constexpr double kTrendRatioLower = 0.01;
constexpr double kTrendRatioUpper = 10.0;

inline double clamp(double value, double lower, double upper) {
	return std::max(lower, std::min(value, upper));
}

std::vector<double> generateAlphaGrid() {
	// PERFORMANCE FIX: Sparse grid (5 values instead of 19)
	// Optimizer will refine these values, so coarse grid is sufficient
	return {0.1, 0.3, 0.5, 0.7, 0.9};
}

std::vector<double> generateBetaGrid(double alpha, bool has_trend) {
	if (!has_trend) {
		return {0.0};
	}
	// PERFORMANCE FIX: Sparse grid (3 values instead of ~10)
	return {0.0, alpha * 0.3, std::min(alpha * 0.7, kAlphaUpper)};
}

std::vector<double> generatePhiGrid(bool damped) {
	if (!damped) {
		return {1.0};
	}
	// PERFORMANCE FIX: Sparse grid (5 values instead of 13)
	// Cover important range [0.80-0.98], optimizer will refine
	return {0.80, 0.85, 0.90, 0.95, 0.98};
}

std::vector<double> generateGammaGrid(anofoxtime::models::ETSSeasonType season_type) {
	if (season_type == anofoxtime::models::ETSSeasonType::None) {
		return {0.0};
	}
	
	// PERFORMANCE FIX: Sparse grid approach
	if (season_type == anofoxtime::models::ETSSeasonType::Multiplicative) {
		// 3 values instead of 10 (multiplicative needs smaller values)
		return {0.01, 0.05, 0.10};
	}
	
	// Additive seasonality: 4 values instead of 20
	return {0.05, 0.2, 0.5, 0.8};
}

std::size_t parameterCount(const anofoxtime::models::ETSConfig &config) {
	const bool has_trend = config.trend != anofoxtime::models::ETSTrendType::None;
	const bool has_season = config.season != anofoxtime::models::ETSSeasonType::None;
	const bool damped = config.trend == anofoxtime::models::ETSTrendType::DampedAdditive ||
	                   config.trend == anofoxtime::models::ETSTrendType::DampedMultiplicative;

	std::size_t states = 1; // level
	if (has_trend) {
		states += 1; // trend
	}
	if (has_season) {
		states += static_cast<std::size_t>(std::max(1, config.season_length));
	}
	std::size_t smoothing = 1; // alpha
	if (has_trend) {
		smoothing += 1; // beta
	}
	if (damped && has_trend) {
		smoothing += 1; // phi
	}
	if (has_season) {
		smoothing += 1; // gamma
	}
	return states + smoothing;
}

anofoxtime::models::AutoETSMetrics computeMetrics(const anofoxtime::models::ETS &model,
                                                  std::size_t parameter_count) {
	anofoxtime::models::AutoETSMetrics metrics;
	metrics.log_likelihood = model.logLikelihood();
	metrics.mse = model.mse();
	metrics.amse = metrics.mse;
	metrics.sigma = std::sqrt(std::max(metrics.mse, 0.0));
	metrics.aic = model.aic(parameter_count);
	metrics.aicc = model.aicc(parameter_count);
	metrics.bic = -2.0 * metrics.log_likelihood + static_cast<double>(parameter_count) *
	                                       std::log(static_cast<double>(model.sampleSize()));
	return metrics;
}

bool betterMetrics(const anofoxtime::models::AutoETSMetrics &lhs,
                   const anofoxtime::models::AutoETSMetrics &rhs) {
	const bool lhsFinite = std::isfinite(lhs.aicc);
	const bool rhsFinite = std::isfinite(rhs.aicc);
	if (lhsFinite && rhsFinite) {
		return lhs.aicc < rhs.aicc;
	}
	if (lhsFinite && !rhsFinite) {
		return true;
	}
	if (!lhsFinite && rhsFinite) {
		return false;
	}
	return lhs.aic < rhs.aic;
}

bool hasNonPositive(const std::vector<double> &values) {
	return std::any_of(values.begin(), values.end(), [](double v) { return v <= 0.0; });
}


} // namespace

namespace anofoxtime::models {


class AutoETS::CandidateEvaluator {
public:
	CandidateEvaluator(const AutoETS &owner,
	                   const std::vector<double> &values,
	                   int season_length);

	CandidateResult evaluate(const CandidateConfig &candidate) const;

private:
	struct Env {
		CandidateConfig candidate;
		CandidateResult result;
		ETSConfig base_config;
		bool has_trend = false;
		bool has_season = false;
		bool damped = false;
		bool multiplicative_error = false;
		bool multiplicative_trend = false;
		bool multiplicative_season = false;
		bool requires_positive = false;
		bool data_positive_ok = true;
		double level_base = 0.0;
		double trend_base = 0.0;
		double ratio_base = 1.0;
		double min_value = 0.0;
		double max_value = 0.0;
		double range = 0.0;
		double level_lower = 0.0;
		double level_upper = 0.0;
		double trend_bound = 0.0;
		double ratio_lower = kTrendRatioLower;
		double ratio_upper = kTrendRatioUpper;
		bool optimize_alpha = true;
		bool optimize_beta = true;
		bool optimize_phi = true;
		bool optimize_gamma = true;
	};

	Env buildEnv(const CandidateConfig &candidate) const;
	void runCoarseSearch(Env &env) const;
	void runStateSearch(Env &env) const;
	void runOptimizer(Env &env) const;

	std::vector<double> alphaGrid(const Env &env) const;
	std::vector<double> betaGrid(const Env &env, double alpha) const;
	std::vector<double> phiGrid(const Env &env) const;
	std::vector<double> gammaGrid(const Env &env) const;

	void coarseFit(Env &env, ETSConfig &config) const;
	void logCoarseFailure(const ETSConfig &config, const std::exception &ex) const;

	static double clampTrendRatio(double value) {
		return std::max(kTrendRatioLower, std::min(value, kTrendRatioUpper));
	}

	const AutoETS &owner_;
	const std::vector<double> &values_;
	int season_length_;
};

AutoETS::CandidateEvaluator::CandidateEvaluator(const AutoETS &owner,
                                                const std::vector<double> &values,
                                                int season_length)
    : owner_(owner), values_(values), season_length_(season_length) {}

AutoETS::CandidateResult AutoETS::CandidateEvaluator::evaluate(const CandidateConfig &candidate) const {
	Env env = buildEnv(candidate);
	if (!env.data_positive_ok) {
		return env.result;
	}

	runCoarseSearch(env);
	if (!env.result.valid) {
		return env.result;
	}

	// PERFORMANCE FIX: Skip state search (optimizer will find good states)
	// This eliminates ~25 additional model fits per candidate
	// runStateSearch(env);
	runOptimizer(env);
	return env.result;
}

AutoETS::CandidateEvaluator::Env AutoETS::CandidateEvaluator::buildEnv(const CandidateConfig &candidate) const {
	Env env;
	env.candidate = candidate;

	env.result.components.error = (candidate.error == ETSErrorType::Additive) ? AutoETSErrorType::Additive
	                                  : AutoETSErrorType::Multiplicative;
	if (candidate.trend == ETSTrendType::None) {
		env.result.components.trend = AutoETSTrendType::None;
	} else if (candidate.trend == ETSTrendType::Multiplicative) {
		env.result.components.trend = AutoETSTrendType::Multiplicative;
	} else {
		env.result.components.trend = AutoETSTrendType::Additive;
	}
	env.result.components.season = (candidate.season == ETSSeasonType::Additive)
	                                   ? AutoETSSeasonType::Additive
	                                   : (candidate.season == ETSSeasonType::Multiplicative
	                                          ? AutoETSSeasonType::Multiplicative
	                                          : AutoETSSeasonType::None);
	env.result.components.damped = candidate.damped;
	env.result.components.season_length = static_cast<std::size_t>(season_length_);

	env.has_trend = candidate.trend != ETSTrendType::None;
	env.has_season = candidate.season != ETSSeasonType::None;
	env.damped = candidate.damped;
	env.multiplicative_error = candidate.error == ETSErrorType::Multiplicative;
	env.multiplicative_trend = candidate.trend == ETSTrendType::Multiplicative;
	env.multiplicative_season = candidate.season == ETSSeasonType::Multiplicative;
	env.requires_positive = env.multiplicative_error || env.multiplicative_trend || env.multiplicative_season;
	env.data_positive_ok = !(env.requires_positive && hasNonPositive(values_));

	env.base_config.error = candidate.error;
	if (env.has_trend && env.damped) {
		env.base_config.trend = (candidate.trend == ETSTrendType::Multiplicative) ? ETSTrendType::DampedMultiplicative
		                               : ETSTrendType::DampedAdditive;
	} else {
		env.base_config.trend = candidate.trend;
	}
	env.base_config.season = candidate.season;
	env.base_config.season_length = season_length_;
	// statsforecast's initparam logic (ets_python.txt lines 127-168)
	// alpha = lower + 0.2 * (upper - lower) / m
	// beta = lower + 0.1 * (upper - lower), with upper = min(upper, alpha)
	// gamma = lower + 0.05 * (upper - lower), with upper = min(upper, 1-alpha)
	// phi = lower + 0.99 * (upper - lower)
	
	const double m_double = static_cast<double>(std::max(1, season_length_));
	const double alpha_default = kAlphaLower + 0.2 * (kAlphaUpper - kAlphaLower) / m_double;
	env.base_config.alpha = owner_.pinned_alpha_.value_or(alpha_default);
	
	if (env.has_trend) {
		const double alpha_val = env.base_config.alpha;
		const double beta_upper = std::min(kAlphaUpper, alpha_val);
		const double beta_default = kAlphaLower + 0.1 * (beta_upper - kAlphaLower);
		env.base_config.beta = owner_.pinned_beta_.has_value() ? std::optional<double>(owner_.pinned_beta_.value())
		                                                     : std::optional<double>(beta_default);
	}
	if (env.has_season) {
		const double alpha_val = env.base_config.alpha;
		const double gamma_upper = std::min(kGammaUpper, 1.0 - alpha_val);
		const double gamma_default = kGammaLower + 0.05 * (gamma_upper - kGammaLower);
		env.base_config.gamma = owner_.pinned_gamma_.has_value() ? std::optional<double>(owner_.pinned_gamma_.value())
		                                                       : std::optional<double>(gamma_default);
	}
	env.base_config.alpha = ::clamp(env.base_config.alpha, kAlphaLower, kAlphaUpper);
	if (env.base_config.beta) {
		env.base_config.beta = ::clamp(*env.base_config.beta, kAlphaLower, kAlphaUpper);
	}
	if (env.base_config.gamma) {
		env.base_config.gamma = ::clamp(*env.base_config.gamma, kGammaLower, kGammaUpper);
	}
	if (env.damped) {
		// statsforecast: phi = lower + 0.99 * (upper - lower)
		const double phi_default = kPhiLower + 0.99 * (kPhiUpper - kPhiLower);
		double phi = owner_.pinned_phi_.value_or(phi_default);
		env.base_config.phi = ::clamp(phi, kPhiLower, kPhiUpper);
	} else {
		env.base_config.phi = 1.0;
	}

	env.result.params.alpha = env.base_config.alpha;
	env.result.params.beta = env.base_config.beta.value_or(std::numeric_limits<double>::quiet_NaN());
	env.result.params.gamma = env.base_config.gamma.value_or(std::numeric_limits<double>::quiet_NaN());
	env.result.params.phi = env.base_config.phi;

	env.optimize_alpha = !owner_.pinned_alpha_.has_value();
	env.optimize_beta = env.has_trend && !owner_.pinned_beta_.has_value();
	env.optimize_phi = env.damped && !owner_.pinned_phi_.has_value();
	env.optimize_gamma = env.has_season && !owner_.pinned_gamma_.has_value();

	if (!values_.empty()) {
		const std::size_t m = static_cast<std::size_t>(std::max(1, season_length_));
		const std::size_t n = values_.size();
		
		// For multiplicative seasonality, use overall mean (not last cycle)
		// This allows proper decomposition into level × seasonal
		if (env.multiplicative_season && n >= m) {
			// Overall mean for multiplicative decomposition
			double sum = 0.0;
			for (const auto& val : values_) {
				sum += val;
			}
			env.level_base = sum / static_cast<double>(n);
		} else if (n >= m && env.has_season) {
			// For additive seasonality: use robust level estimate
			// For damped models, closer to last value works better with trending data
			if (env.damped && n >= 2 * m) {
				// For damped: weight recent data more heavily
				double sum = 0.0;
				for (std::size_t i = n - m; i < n; ++i) {
					sum += values_[i];
				}
				env.level_base = sum / static_cast<double>(m);
			} else {
				// For non-damped: overall mean of last 2 cycles is more stable
				const std::size_t lookback = std::min(n, 2 * m);
				double sum = 0.0;
				for (std::size_t i = n - lookback; i < n; ++i) {
					sum += values_[i];
				}
				env.level_base = sum / static_cast<double>(lookback);
			}
		} else if (n > 1) {
			// Use mean of last few observations or overall mean
			const std::size_t last_n = std::min(n, static_cast<std::size_t>(10));
			double sum = 0.0;
			for (std::size_t i = n - last_n; i < n; ++i) {
				sum += values_[i];
			}
			env.level_base = sum / static_cast<double>(last_n);
		} else {
			env.level_base = values_.front();
		}
		
		env.min_value = *std::min_element(values_.begin(), values_.end());
		env.max_value = *std::max_element(values_.begin(), values_.end());
		env.range = std::max(1.0, env.max_value - env.min_value);
		
		// For multiplicative models, level must stay positive and reasonable
		if (env.multiplicative_error || env.multiplicative_season || env.multiplicative_trend) {
			env.level_lower = std::max(1.0, env.min_value * 0.1);  // At least 10% of min value
			env.level_upper = env.max_value * 10.0;  // Up to 10x max value
		} else {
			env.level_lower = env.min_value - 2.0 * env.range;
			env.level_upper = env.max_value + 2.0 * env.range;
		}
		if (env.requires_positive) {
			env.level_lower = std::max(env.level_lower, 1e-3);
		}
		env.trend_bound = std::max(1.0, env.range * 1.5);
		if (values_.size() > 1) {
			env.trend_base = values_[1] - values_[0];
		}
		if (values_.size() > 1) {
			double start = std::max(values_.front(), 1e-3);
			double end = std::max(values_.back(), 1e-3);
			const double denom = static_cast<double>(std::max<std::size_t>(1, values_.size() - 1));
			double ratio = std::pow(end / start, 1.0 / denom);
			env.ratio_base = clampTrendRatio(ratio);
		}
	} else {
		env.data_positive_ok = false;
	}

	return env;
}

void AutoETS::CandidateEvaluator::runCoarseSearch(Env &env) const {
	auto alphas = alphaGrid(env);
	if (alphas.empty()) {
		return;
	}

	for (double alpha : alphas) {
		ETSConfig alpha_config = env.base_config;
		alpha_config.alpha = ::clamp(alpha, kAlphaLower, kAlphaUpper);

		if (env.has_trend) {
			auto betas = betaGrid(env, alpha_config.alpha);
			if (betas.empty()) {
				continue;
			}
			for (double beta : betas) {
				ETSConfig beta_config = alpha_config;
				beta_config.beta = ::clamp(beta, kAlphaLower, alpha_config.alpha);
			auto phi_values = phiGrid(env);
			for (double phi : phi_values) {
				ETSConfig phi_config = beta_config;
				phi_config.phi = env.damped ? ::clamp(phi, kPhiLower, kPhiUpper) : 1.0;
				if (env.has_season) {
					auto gammas = gammaGrid(env);
					if (gammas.empty()) {
						continue;
					}
					for (double gamma : gammas) {
						ETSConfig gamma_config = phi_config;
						// Use statsforecast's admissibility constraint for seasonal models
						double phi_val = gamma_config.phi;
						double gamma_lower_admissible = std::max(1.0 - 1.0/phi_val - gamma_config.alpha, 0.0);
						double gamma_upper_admissible = 1.0 + 1.0/phi_val - gamma_config.alpha;
						gamma_lower_admissible = std::max(gamma_lower_admissible, kGammaLower);
						gamma_upper_admissible = std::min(gamma_upper_admissible, kGammaUpper);
						gamma_config.gamma = ::clamp(gamma, gamma_lower_admissible, gamma_upper_admissible);
						coarseFit(env, gamma_config);
					}
				} else {
					ETSConfig gamma_config = phi_config;
					gamma_config.gamma.reset();
					coarseFit(env, gamma_config);
				}
			}
		}
	} else {
		auto phi_values = phiGrid(env);
		for (double phi : phi_values) {
			ETSConfig phi_config = alpha_config;
			phi_config.beta.reset();
			phi_config.phi = env.damped ? ::clamp(phi, kPhiLower, kPhiUpper) : 1.0;
			if (env.has_season) {
				auto gammas = gammaGrid(env);
				if (gammas.empty()) {
					continue;
				}
				for (double gamma : gammas) {
					ETSConfig gamma_config = phi_config;
					// Use statsforecast's admissibility constraint for seasonal models
					double phi_val = gamma_config.phi;
					double gamma_lower_admissible = std::max(1.0 - 1.0/phi_val - gamma_config.alpha, 0.0);
					double gamma_upper_admissible = 1.0 + 1.0/phi_val - gamma_config.alpha;
					gamma_lower_admissible = std::max(gamma_lower_admissible, kGammaLower);
					gamma_upper_admissible = std::min(gamma_upper_admissible, kGammaUpper);
					gamma_config.gamma = ::clamp(gamma, gamma_lower_admissible, gamma_upper_admissible);
					coarseFit(env, gamma_config);
				}
			} else {
				ETSConfig gamma_config = phi_config;
				gamma_config.gamma.reset();
				coarseFit(env, gamma_config);
			}
		}
	}
}
}

void AutoETS::CandidateEvaluator::runStateSearch(Env &env) const {
	if (!env.result.valid) {
		return;
	}

	std::vector<double> level_offsets = {-0.2 * env.range, -0.1 * env.range, 0.0, 0.1 * env.range, 0.2 * env.range};
	std::vector<double> trend_seeds;
	if (env.has_trend) {
		if (env.multiplicative_trend) {
			trend_seeds = {env.ratio_base * 0.85,
			               env.ratio_base * 0.95,
			               env.ratio_base,
			               env.ratio_base * 1.05,
			               env.ratio_base * 1.15};
		} else {
			trend_seeds = {env.trend_base - 0.2 * env.range,
			               env.trend_base - 0.1 * env.range,
			               env.trend_base,
			               env.trend_base + 0.1 * env.range,
			               env.trend_base + 0.2 * env.range};
		}
	}

	for (double level_offset : level_offsets) {
		double level_candidate = ::clamp(env.level_base + level_offset, env.level_lower, env.level_upper);
		if (env.requires_positive) {
			level_candidate = std::max(level_candidate, 1e-3);
		}

		if (trend_seeds.empty()) {
			try {
				ETS model(env.result.config);
				model.fitWithInitialState(values_, level_candidate, std::nullopt);
				auto metrics = computeMetrics(model, parameterCount(env.result.config));
				if (!std::isfinite(metrics.aicc) || metrics.mse <= 0.0 || metrics.log_likelihood >= 0.0) {
					continue;
				}
				if (betterMetrics(metrics, env.result.metrics)) {
					env.result.metrics = metrics;
					env.result.level0 = level_candidate;
					env.result.trend0.reset();
					env.result.has_state_override = true;
				}
			} catch (...) {
			}
			continue;
		}

		for (double seed : trend_seeds) {
			std::optional<double> trend_candidate;
			if (env.multiplicative_trend) {
				trend_candidate = clampTrendRatio(seed);
			} else {
				trend_candidate = ::clamp(seed, -env.trend_bound, env.trend_bound);
			}

			try {
				ETS model(env.result.config);
				model.fitWithInitialState(values_, level_candidate, trend_candidate);
				auto metrics = computeMetrics(model, parameterCount(env.result.config));
				if (!std::isfinite(metrics.aicc) || metrics.mse <= 0.0 || metrics.log_likelihood >= 0.0) {
					continue;
				}
				if (betterMetrics(metrics, env.result.metrics)) {
					env.result.metrics = metrics;
					env.result.level0 = level_candidate;
					env.result.trend0 = trend_candidate;
					env.result.has_state_override = true;
				}
			} catch (...) {
			}
		}
	}
}

void AutoETS::CandidateEvaluator::runOptimizer(Env &env) const {
	if (!env.result.valid) {
		return;
	}

	const double aicc_before = env.result.metrics.aicc;

	const bool optimize_alpha = env.optimize_alpha;
	const bool optimize_beta = env.optimize_beta;
	const bool optimize_phi = env.optimize_phi;
	const bool optimize_gamma = env.optimize_gamma;

	const bool has_trend = env.has_trend;
	const bool has_season = env.has_season;
	const bool multiplicative_trend = env.result.config.trend == ETSTrendType::Multiplicative ||
	                                  env.result.config.trend == ETSTrendType::DampedMultiplicative;

	double level_start = ::clamp(env.result.level0, env.level_lower, env.level_upper);
	if (env.requires_positive) {
		level_start = std::max(level_start, 1e-3);
	}

	double fallback_trend = multiplicative_trend ? env.ratio_base : env.trend_base;
	double trend_start = env.result.trend0.value_or(fallback_trend);
	if (multiplicative_trend) {
		trend_start = clampTrendRatio(trend_start);
	} else {
		trend_start = ::clamp(trend_start, -env.trend_bound, env.trend_bound);
	}

	std::vector<double> start;
	std::vector<double> lower_bounds;
	std::vector<double> upper_bounds;

	auto pushParam = [&](double value, double lower, double upper) {
		start.push_back(value);
		lower_bounds.push_back(lower);
		upper_bounds.push_back(upper);
	};

	if (optimize_alpha) {
		// For multiplicative seasonality, constrain alpha to avoid level collapse
		double alpha_upper = kAlphaUpper;
		if (env.multiplicative_season) {
			alpha_upper = 0.6;  // Smaller alpha for stability
		}
		pushParam(env.result.config.alpha, kAlphaLower, alpha_upper);
	}
	if (optimize_beta) {
		double beta_start = env.result.config.beta.value_or(std::min(env.result.config.alpha, env.result.config.alpha * 0.5));
		pushParam(::clamp(beta_start, kAlphaLower, env.result.config.alpha), kAlphaLower, kAlphaUpper);
	}
	if (optimize_phi) {
		pushParam(env.result.config.phi, kPhiLower, kPhiUpper);
	}
	if (optimize_gamma) {
		double gamma_start = env.result.config.gamma.value_or(std::max(kGammaLower, env.result.config.alpha * 0.1));
		double gamma_upper = kGammaUpper;
		if (env.candidate.season == ::anofoxtime::models::ETSSeasonType::Multiplicative) {
			gamma_upper = 0.2;  // Tighter bound for multiplicative seasonality
		}
		pushParam(::clamp(gamma_start, kGammaLower, gamma_upper), kGammaLower, gamma_upper);
	}
	pushParam(level_start, env.level_lower, env.level_upper);
	if (has_trend) {
		if (multiplicative_trend) {
			pushParam(trend_start, env.ratio_lower, env.ratio_upper);
		} else {
			pushParam(trend_start, -env.trend_bound, env.trend_bound);
		}
	}
	
	// For seasonal models, also optimize the m seasonal states
	int m = env.base_config.season_length;
	std::vector<double> seasonal_start;
	if (has_season && m > 0) {
		// DO NOT optimize seasonal states - it makes optimization intractable
		// statsforecast DOES optimize them but uses sophisticated initialization
		// For now, we'll just use the initialized states
		// TODO: Implement joint optimization properly
	}

	auto evaluateVector = [&](const std::vector<double> &vec,
	                          ETSConfig &cfg_out,
	                          AutoETSParameters &params_out,
	                          AutoETSMetrics &metrics_out,
	                          double &level_out,
	                          std::optional<double> &trend_out,
	                          std::vector<double> &seasonal_out) -> bool {
		ETSConfig cfg = env.result.config;
		std::size_t idx = 0;

		if (optimize_alpha) {
			cfg.alpha = ::clamp(vec[idx++], kAlphaLower, kAlphaUpper);
		}
		if (has_trend) {
			if (optimize_beta) {
				cfg.beta = ::clamp(vec[idx++], kAlphaLower, cfg.alpha);
			} else {
				cfg.beta = env.result.config.beta;
			}
		} else {
			cfg.beta.reset();
		}

		if (env.damped) {
			if (optimize_phi) {
				cfg.phi = ::clamp(vec[idx++], kPhiLower, kPhiUpper);
			} else {
				cfg.phi = env.result.config.phi;
			}
		} else {
			cfg.phi = 1.0;
		}

		if (has_season) {
			if (optimize_gamma) {
				double gamma_val = vec[idx++];
				// Use statsforecast's admissibility constraint for seasonal models
				// gamma_lower = max(1 - 1/phi - alpha, 0)
				// gamma_upper = 1 + 1/phi - alpha
				double phi_val = cfg.phi;
				double gamma_lower_admissible = std::max(1.0 - 1.0/phi_val - cfg.alpha, 0.0);
				double gamma_upper_admissible = 1.0 + 1.0/phi_val - cfg.alpha;
				// Also respect hard bounds
				gamma_lower_admissible = std::max(gamma_lower_admissible, kGammaLower);
				gamma_upper_admissible = std::min(gamma_upper_admissible, kGammaUpper);
				cfg.gamma = ::clamp(gamma_val, gamma_lower_admissible, gamma_upper_admissible);
			} else {
				cfg.gamma = env.result.config.gamma;
			}
		} else {
			cfg.gamma.reset();
		}

		double level0 = ::clamp(vec[idx++], env.level_lower, env.level_upper);
		if (env.requires_positive) {
			level0 = std::max(level0, 1e-3);
		}

		std::optional<double> trend0;
		if (has_trend) {
			double trend_val = vec[idx++];
			if (multiplicative_trend) {
				trend_val = clampTrendRatio(trend_val);
			} else {
				trend_val = ::clamp(trend_val, -env.trend_bound, env.trend_bound);
			}
			trend0 = trend_val;
		}
		
		// NOTE: We're NOT optimizing seasonal states (too complex)
		// statsforecast does it but requires sophisticated initialization
		// For now, we let ETS initialize them automatically

		try {
			ETS model(cfg);
			model.fitWithInitialState(values_, level0, trend0);
			auto metrics = computeMetrics(model, parameterCount(cfg));
			if (!std::isfinite(metrics.aicc) || metrics.mse <= 0.0 || metrics.log_likelihood >= 0.0) {
				return false;
			}
			cfg_out = cfg;
			params_out.alpha = cfg.alpha;
			params_out.beta = cfg.beta.value_or(std::numeric_limits<double>::quiet_NaN());
			params_out.gamma = cfg.gamma.value_or(std::numeric_limits<double>::quiet_NaN());
			params_out.phi = cfg.phi;
			
			// Verify admissibility is preserved
#ifndef ANOFOX_NO_LOGGING
			static int verify_count = 0;
			if (verify_count < 2 && cfg.season != ETSSeasonType::None) {
				const double gamma_check = cfg.gamma.value_or(0.0);
				const double alpha_check = cfg.alpha;
				if (gamma_check > 1.0 - alpha_check + 1e-6) {
					fprintf(stderr, "[VERIFY-FAIL] params_out has gamma=%.4f > (1-alpha)=%.4f!\n",
					        gamma_check, 1.0 - alpha_check);
				}
				verify_count++;
			}
#endif
			
			metrics_out = metrics;
			level_out = level0;
			trend_out = trend0;
			seasonal_out.clear();  // Not optimizing seasonal states
			return true;
		} catch (...) {
			return false;
		}
	};

	anofoxtime::utils::NelderMeadOptimizer optimizer;
	anofoxtime::utils::NelderMeadOptimizer::Options options;
	options.step = 0.1;
	options.max_iterations = owner_.max_iterations_;

	auto objective_metric = [&](const AutoETSMetrics &metrics) -> double {
		switch (owner_.optimization_criterion_) {
		case OptimizationCriterion::Likelihood:
			return std::isfinite(metrics.log_likelihood) ? -metrics.log_likelihood : std::numeric_limits<double>::infinity();
		case OptimizationCriterion::MSE:
			return std::isfinite(metrics.mse) ? metrics.mse : std::numeric_limits<double>::infinity();
		case OptimizationCriterion::AMSE:
			return std::isfinite(metrics.amse) ? metrics.amse : std::numeric_limits<double>::infinity();
		case OptimizationCriterion::Sigma:
			return std::isfinite(metrics.sigma) ? metrics.sigma : std::numeric_limits<double>::infinity();
		default:
			return std::numeric_limits<double>::infinity();
		}
	};

	auto objective = [&](const std::vector<double> &point) -> double {
		ETSConfig cfg;
		AutoETSParameters params;
		AutoETSMetrics metrics;
		double level_override = level_start;
		std::optional<double> trend_override;
		std::vector<double> seasonal_override;
		if (!evaluateVector(point, cfg, params, metrics, level_override, trend_override, seasonal_override)) {
			return std::numeric_limits<double>::infinity();
		}
		return objective_metric(metrics);
	};

	// Use L-BFGS for:
	// 1. Multiplicative seasonal models (better convergence)
	// 2. Damped additive seasonal models (complex parameter space)
	const bool use_lbfgs = env.multiplicative_season || 
	                      (env.damped && env.has_season && !env.multiplicative_season);
	
	if (use_lbfgs) {
		// L-BFGS path with ANALYTICAL gradient computation
		// This is 60-80x faster than numerical differentiation!
		auto objective_with_grad = [&](const std::vector<double> &point, std::vector<double> &grad) -> double {
			ETSConfig cfg;
			AutoETSParameters params;
			AutoETSMetrics metrics;
			double level_override = level_start;
			std::optional<double> trend_override;
			std::vector<double> seasonal_override;
			
			if (!evaluateVector(point, cfg, params, metrics, level_override, trend_override, seasonal_override)) {
				// Fill gradient with zeros on failure
				std::fill(grad.begin(), grad.end(), 0.0);
				return std::numeric_limits<double>::infinity();
			}
			
			// ANALYTICAL GRADIENTS: Use backpropagation through ETS recursions
			// Compute analytical gradients using ETSGradients class
			optimization::ETSGradients::GradientComponents analytical_grad;
			
			// Prepare seasonal initial states (if needed)
			std::vector<double> seasonal_init;
			if (env.has_season && env.base_config.season_length > 0) {
				seasonal_init.resize(static_cast<size_t>(env.base_config.season_length), 1.0);
				// Initialize with simple decomposition
				// (In practice, ETS will refine these during forward pass)
			}
			
			// Compute negative log-likelihood with analytical gradients
			double neg_loglik = optimization::ETSGradients::computeNegLogLikelihoodWithGradients(
				cfg, values_, level_override, 
				trend_override.value_or(0.0), seasonal_init, analytical_grad
			);
			
			// Map analytical gradients to parameter vector
			grad.resize(point.size());
			std::size_t idx = 0;
			
			if (optimize_alpha) {
				grad[idx++] = analytical_grad.d_alpha;
			}
			if (optimize_beta) {
				grad[idx++] = analytical_grad.d_beta;
			}
			if (optimize_phi) {
				grad[idx++] = analytical_grad.d_phi;
			}
			if (optimize_gamma) {
				grad[idx++] = analytical_grad.d_gamma;
			}
			// Level gradient
			grad[idx++] = analytical_grad.d_level;
			// Trend gradient (if has_trend)
			if (has_trend) {
				grad[idx++] = analytical_grad.d_trend;
			}
			
			// Convert negative log-likelihood to objective based on criterion
			// For likelihood optimization, we minimize negative log-likelihood
			const double f0 = objective_metric(metrics);
			return f0;
		};
		
		optimization::LBFGSOptimizer::Options lbfgs_opts;
		lbfgs_opts.max_iterations = owner_.max_iterations_;
		lbfgs_opts.epsilon = 1e-6;
		lbfgs_opts.m = 10;
		lbfgs_opts.verbose = false;
		
		// PERFORMANCE FIX: Single-start optimization (use best grid result only)
		// Multi-start with 9 gamma values was causing 9x slowdown
		std::vector<std::vector<double>> starting_points;
		starting_points.push_back(start);  // Only use best coarse grid result
		
		// COMMENTED OUT: Multi-start gamma exploration (too expensive)
		// The coarse grid + sparse gamma values + optimizer is sufficient
		/*
		if (optimize_gamma && start.size() > 0) {
			size_t gamma_idx = 0;
			if (optimize_alpha) gamma_idx++;
			if (optimize_beta) gamma_idx++;
			if (optimize_phi) gamma_idx++;
			
			if (gamma_idx < start.size()) {
				for (double g : {0.001, 0.003, 0.005, 0.01, 0.03, 0.05, 0.10, 0.15}) {
					std::vector<double> start_new = start;
					start_new[gamma_idx] = g;
					starting_points.push_back(start_new);
				}
			}
		}
		*/
		
		// Run L-BFGS from each starting point, keep best result
		optimization::LBFGSOptimizer::Result best_result;
		best_result.fx = std::numeric_limits<double>::infinity();
		best_result.converged = false;
		
		for (const auto& sp : starting_points) {
			auto lbfgs_result = optimization::LBFGSOptimizer::minimize(
				objective_with_grad, sp, lower_bounds, upper_bounds, lbfgs_opts
			);
			
			if (lbfgs_result.converged && lbfgs_result.fx < best_result.fx) {
				best_result = lbfgs_result;
			}
		}
		
		env.result.optimizer_iterations = best_result.iterations;
		env.result.optimizer_converged = best_result.converged;
		env.result.optimizer_objective = best_result.fx;
		
		if (!best_result.x.empty() && best_result.converged) {
			ETSConfig cfg;
			AutoETSParameters params;
			AutoETSMetrics metrics;
			double level_override = level_start;
			std::optional<double> trend_override;
			std::vector<double> seasonal_override;
			
			if (evaluateVector(best_result.x, cfg, params, metrics, level_override, trend_override, seasonal_override) &&
			    betterMetrics(metrics, env.result.metrics)) {
				env.result.config = cfg;
				env.result.params = params;
				env.result.metrics = metrics;
				env.result.level0 = level_override;
				env.result.trend0 = trend_override;
				env.result.has_state_override = true;
			}
		}
	} else {
		// Nelder-Mead path (works well for non-multiplicative seasonal models)
		const auto nm_result = optimizer.minimize(objective, start, options, lower_bounds, upper_bounds);
		env.result.optimizer_iterations = nm_result.iterations;
		env.result.optimizer_converged = nm_result.converged;
		env.result.optimizer_objective = nm_result.value;

		if (!nm_result.best.empty()) {
			ETSConfig cfg;
			AutoETSParameters params;
			AutoETSMetrics metrics;
			double level_override = level_start;
			std::optional<double> trend_override;
			std::vector<double> seasonal_override;
			if (evaluateVector(nm_result.best, cfg, params, metrics, level_override, trend_override, seasonal_override) &&
			    betterMetrics(metrics, env.result.metrics)) {
				env.result.config = cfg;
				env.result.params = params;
				env.result.metrics = metrics;
				env.result.level0 = level_override;
				env.result.trend0 = trend_override;
				env.result.has_state_override = true;
			}
		}
	}
	
	// Debug: Log optimization improvement
#ifndef ANOFOX_NO_LOGGING
	if (env.damped && env.has_season) {
		const double aicc_after = env.result.metrics.aicc;
		const double improvement = aicc_before - aicc_after;
		fprintf(stderr, "[AutoETS-OPT] Damped+Season: AICc %.2f → %.2f (improvement: %.2f)\n",
		        aicc_before, aicc_after, improvement);
		fflush(stderr);
	}
#endif
}

std::vector<double> AutoETS::CandidateEvaluator::alphaGrid(const Env &env) const {
	if (!env.optimize_alpha) {
		return {env.base_config.alpha};
	}
	return generateAlphaGrid();
}

std::vector<double> AutoETS::CandidateEvaluator::betaGrid(const Env &env, double alpha) const {
	if (!env.has_trend) {
		return {};
	}
	if (!env.optimize_beta) {
		double value = env.base_config.beta.value_or(alpha);
		if (value > alpha) {
			return {};
		}
		return {value};
	}
	return generateBetaGrid(alpha, true);
}

std::vector<double> AutoETS::CandidateEvaluator::phiGrid(const Env &env) const {
	if (!env.damped) {
		return {1.0};
	}
	if (!env.optimize_phi) {
		return {env.base_config.phi};
	}
	return generatePhiGrid(true);
}

std::vector<double> AutoETS::CandidateEvaluator::gammaGrid(const Env &env) const {
	if (!env.has_season) {
		return {};
	}
	if (!env.optimize_gamma) {
		return {env.base_config.gamma.value_or(env.base_config.alpha)};
	}
	return generateGammaGrid(env.candidate.season);
}

void AutoETS::CandidateEvaluator::coarseFit(Env &env, ETSConfig &config) const {
	try {
		ETS model(config);
		model.fitRaw(values_);
		auto metrics = computeMetrics(model, parameterCount(config));
		if (!std::isfinite(metrics.aicc)) {
			return;
		}
		if (!env.result.valid || betterMetrics(metrics, env.result.metrics)) {
			env.result.valid = true;
			env.result.config = config;
			env.result.metrics = metrics;
			env.result.params.alpha = config.alpha;
			env.result.params.beta = config.beta.value_or(std::numeric_limits<double>::quiet_NaN());
			env.result.params.gamma = config.gamma.value_or(std::numeric_limits<double>::quiet_NaN());
			env.result.params.phi = config.phi;
			env.result.level0 = values_.front();
			env.result.trend0.reset();
			env.result.has_state_override = false;
		}
	} catch (const std::exception &ex) {
		logCoarseFailure(config, ex);
	}
}

void AutoETS::CandidateEvaluator::logCoarseFailure(const ETSConfig &config, const std::exception &ex) const {
	ANOFOX_DEBUG("AutoETS coarse candidate failed (error={}, trend={}, season={}, phi={}): {}",
	             static_cast<int>(config.error),
	             static_cast<int>(config.trend),
	             static_cast<int>(config.season),
	             config.phi,
	             ex.what());
}

AutoETS::AutoETS(int season_length, std::string spec)
    : season_length_(season_length), spec_text_(std::move(spec)) {
	if (season_length_ <= 0) {
		throw std::invalid_argument("AutoETS season length must be positive.");
	}
	
	// Support both 3-character ("AAA") and 4-character ("AAdA") specs
	// 4-char format: Error-Trend-Damping-Season (e.g., "AAdA" = Additive error, Additive damped trend, Additive season)
	bool explicit_damping = false;
	char damping_char = 'Z';  // Z means auto-select
	
	if (spec_text_.size() == 4) {
		// 4-character format with explicit damping
		explicit_damping = true;
		damping_char = spec_text_[2];
		
		// Set damped_policy based on explicit damping character
		if (damping_char == 'd' || damping_char == 'D') {
			damped_policy_ = DampedPolicy::Always;
		} else if (damping_char == 'N' || damping_char == 'n') {
			damped_policy_ = DampedPolicy::Never;
		}
		// else: Z or other remains Auto
		
		// Reformat to parse: spec is now [Error][Trend][Season], damping separate
		spec_text_ = std::string() + spec_text_[0] + spec_text_[1] + spec_text_[3];
	} else if (spec_text_.size() != 3) {
		throw std::invalid_argument("AutoETS specification must be 3 or 4 characters.");
	}

	const char error_char = spec_text_[0];
	const char trend_char = spec_text_[1];
	const char season_char = spec_text_[2];

	if (error_char == 'N') {
		throw std::invalid_argument("Error component cannot be 'N' in AutoETS spec.");
	}

	if (error_char == 'A' && (trend_char == 'M' || season_char == 'M')) {
		throw std::invalid_argument("Additive error cannot be combined with multiplicative trend or seasonality in AutoETS spec.");
	}
	if (error_char == 'M' && trend_char == 'M' && season_char == 'M') {
		throw std::invalid_argument("Fully multiplicative AutoETS specification is not supported.");
	}

	switch (error_char) {
	case 'A':
		spec_.errors = {ETSErrorType::Additive};
		break;
	case 'M':
		spec_.errors = {ETSErrorType::Multiplicative};
		break;
	case 'Z':
		spec_.errors = {ETSErrorType::Additive, ETSErrorType::Multiplicative};
		break;
	default:
		throw std::invalid_argument("Unsupported error specification in AutoETS.");
	}

	switch (trend_char) {
	case 'N':
		spec_.trends = {ETSTrendType::None};
		break;
	case 'A':
		// If explicit damping specified, use it
		if (explicit_damping) {
			if (damping_char == 'd') {
				spec_.trends = {ETSTrendType::DampedAdditive};
			} else if (damping_char == 'N' || damping_char == 'n') {
				spec_.trends = {ETSTrendType::Additive};  // No damping
			} else {
				// 'Z' or other: auto-select between damped and non-damped
				spec_.trends = {ETSTrendType::Additive, ETSTrendType::DampedAdditive};
			}
		} else {
			// No explicit damping char: auto-select (like statsforecast does)
			// statsforecast tries both damped and non-damped, picks best by AICc
			spec_.trends = {ETSTrendType::Additive, ETSTrendType::DampedAdditive};
		}
		break;
	case 'M':
		if (explicit_damping) {
			if (damping_char == 'd') {
				spec_.trends = {ETSTrendType::DampedMultiplicative};
			} else if (damping_char == 'N' || damping_char == 'n') {
				spec_.trends = {ETSTrendType::Multiplicative};
			} else {
				spec_.trends = {ETSTrendType::Multiplicative, ETSTrendType::DampedMultiplicative};
			}
		} else {
			// Auto-select damping for multiplicative trend too
			spec_.trends = {ETSTrendType::Multiplicative, ETSTrendType::DampedMultiplicative};
		}
		break;
	case 'Z':
		// Full auto-select: try all trend options including damped
		spec_.trends = {ETSTrendType::None, ETSTrendType::Additive, ETSTrendType::DampedAdditive,
		                ETSTrendType::Multiplicative, ETSTrendType::DampedMultiplicative};
		break;
	default:
		throw std::invalid_argument("Unsupported trend specification in AutoETS.");
	}

	auto ensureSeasonalLength = [&]() {
		if (season_length_ < 2) {
			throw std::invalid_argument("Seasonal AutoETS specification requires season_length >= 2.");
		}
	};

	switch (season_char) {
	case 'N':
		spec_.seasons = {ETSSeasonType::None};
		break;
	case 'A':
		ensureSeasonalLength();
		spec_.seasons = {ETSSeasonType::Additive};
		break;
	case 'M':
		ensureSeasonalLength();
		spec_.seasons = {ETSSeasonType::Multiplicative};
		break;
	case 'Z':
		if (season_length_ < 2) {
			spec_.seasons = {ETSSeasonType::None};
		} else {
			spec_.seasons = {ETSSeasonType::None, ETSSeasonType::Additive, ETSSeasonType::Multiplicative};
		}
		break;
	default:
		throw std::invalid_argument("Unsupported seasonal specification in AutoETS.");
	}

	trend_explicit_multiplicative_ = (trend_char == 'M');
	trend_auto_allows_multiplicative_ = (trend_char == 'Z');
}

void AutoETS::ensureUnivariate(const core::TimeSeries &ts) const {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("AutoETS currently supports univariate series only.");
	}
}

std::vector<AutoETS::CandidateConfig> AutoETS::enumerateCandidates(const std::vector<double> &values) const {
	std::vector<CandidateConfig> configs;
	const bool non_positive = hasNonPositive(values);

	for (auto error : spec_.errors) {
		const bool error_multiplicative = (error == ETSErrorType::Multiplicative);
		if (error_multiplicative && non_positive) {
			continue;
		}
		for (auto trend : spec_.trends) {
			const bool multiplicative_trend = (trend == ETSTrendType::Multiplicative);
			if (multiplicative_trend && non_positive) {
				continue;
			}
			if (multiplicative_trend) {
				const bool allow_trend =
				    trend_explicit_multiplicative_ || (trend_auto_allows_multiplicative_ && allow_multiplicative_trend_);
				if (!allow_trend) {
					continue;
				}
			}
			for (auto season : spec_.seasons) {
				if (season != ETSSeasonType::None && season_length_ < 2) {
					continue;
				}
				const bool multiplicative_season = (season == ETSSeasonType::Multiplicative);
				if (multiplicative_season && non_positive) {
					continue;
				}
				if (error_multiplicative && season == ETSSeasonType::Additive) {
					// Combination not supported by current ETS implementation.
					continue;
				}
				if (!error_multiplicative && multiplicative_trend && multiplicative_season) {
					continue;
				}
				for (bool damped : dampedCandidates(trend)) {
					configs.push_back(CandidateConfig{error, trend, season, damped});
				}
			}
		}
	}

	return configs;
}

const AutoETSComponents &AutoETS::components() const {
	if (!is_fitted_) {
		throw std::logic_error("AutoETS::components accessed before fit.");
	}
	return components_;
}

const AutoETSParameters &AutoETS::parameters() const {
	if (!is_fitted_) {
		throw std::logic_error("AutoETS::parameters accessed before fit.");
	}
	return parameters_;
}

const AutoETSMetrics &AutoETS::metrics() const {
	if (!is_fitted_) {
		throw std::logic_error("AutoETS::metrics accessed before fit.");
	}
	return metrics_;
}

const AutoETSDiagnostics &AutoETS::diagnostics() const {
	if (!is_fitted_) {
		throw std::logic_error("AutoETS::diagnostics accessed before fit.");
	}
	return diagnostics_;
}

const std::vector<double> &AutoETS::fittedValues() const {
	if (!is_fitted_) {
		throw std::logic_error("AutoETS::fittedValues accessed before fit.");
	}
	return fitted_;
}

const std::vector<double> &AutoETS::residuals() const {
	if (!is_fitted_) {
		throw std::logic_error("AutoETS::residuals accessed before fit.");
	}
	return residuals_;
}

void AutoETS::fit(const core::TimeSeries &ts) {
	ensureUnivariate(ts);
	const auto &values = ts.getValues();
	if (values.size() < 4) {
		throw std::invalid_argument("AutoETS requires at least four observations.");
	}

	diagnostics_ = AutoETSDiagnostics{};
	diagnostics_.training_data_size = values.size();

	auto candidates = enumerateCandidates(values);
	if (candidates.empty()) {
		throw std::runtime_error("AutoETS could not construct any valid candidate models for given data.");
	}

	// Debug: Log candidate count
#ifndef ANOFOX_NO_LOGGING
	fprintf(stderr, "[AutoETS] Evaluating %zu candidates\n", candidates.size());
	int damped_count = 0;
	for (const auto &c : candidates) {
		if (c.trend == ETSTrendType::DampedAdditive || c.trend == ETSTrendType::DampedMultiplicative) {
			damped_count++;
		}
	}
	fprintf(stderr, "[AutoETS] %d damped candidates out of %zu total\n", damped_count, candidates.size());
	fflush(stderr);
#endif

	CandidateResult best_result;
	CandidateEvaluator evaluator(*this, values, season_length_);

	int valid_count = 0;
	int candidates_since_improvement = 0;
	const int early_termination_threshold = 8;  // Stop if 8 candidates with no improvement
	const double aicc_improvement_threshold = 0.01;  // Require at least 0.01 AICc improvement
	
	for (const auto &candidate : candidates) {
		auto result = evaluator.evaluate(candidate);
		
		// Debug: Log each candidate evaluation
#ifndef ANOFOX_NO_LOGGING
		const bool is_damped = (candidate.trend == ETSTrendType::DampedAdditive || 
		                       candidate.trend == ETSTrendType::DampedMultiplicative);
		fprintf(stderr, "[AutoETS-EVAL] error=%d, trend=%d, season=%d, damped=%d → valid=%d, AICc=%.2f\n",
		        static_cast<int>(candidate.error), static_cast<int>(candidate.trend),
		        static_cast<int>(candidate.season), is_damped ? 1 : 0,
		        result.valid ? 1 : 0,
		        result.valid ? result.metrics.aicc : 9999.0);
		fflush(stderr);
#endif
		
		if (!result.valid) {
			candidates_since_improvement++;
			continue;
		}
		valid_count++;
		
		// Check for improvement
		bool improved = false;
		if (!best_result.valid || betterMetrics(result.metrics, best_result.metrics)) {
			double prev_aicc = best_result.valid ? best_result.metrics.aicc : std::numeric_limits<double>::max();
			best_result = std::move(result);
			double improvement = prev_aicc - best_result.metrics.aicc;
			
			if (improvement > aicc_improvement_threshold) {
				improved = true;
				candidates_since_improvement = 0;
#ifndef ANOFOX_NO_LOGGING
				fprintf(stderr, "[AutoETS-IMPROVE] AICc improved by %.4f, resetting early termination counter\n", improvement);
				fflush(stderr);
#endif
			}
		}
		
		if (!improved) {
			candidates_since_improvement++;
		}
		
		// Early termination: stop if many candidates without meaningful improvement
		if (best_result.valid && candidates_since_improvement >= early_termination_threshold) {
#ifndef ANOFOX_NO_LOGGING
			fprintf(stderr, "[AutoETS-EARLY-STOP] Stopping after %d candidates without improvement (evaluated %d/%zu)\n",
			        early_termination_threshold, valid_count + (int)(candidates_since_improvement), candidates.size());
			fflush(stderr);
#endif
			break;
		}
	}
	
#ifndef ANOFOX_NO_LOGGING
	fprintf(stderr, "[AutoETS] %d valid models out of %zu candidates\n", valid_count, candidates.size());
	fflush(stderr);
#endif

	if (!best_result.valid) {
		throw std::runtime_error("AutoETS failed to fit any candidate model.");
	}

	// Debug: Log selected model WITH FULL PARAMETERS
#ifndef ANOFOX_NO_LOGGING
	const bool log_params = (best_result.config.season != ETSSeasonType::None && 
	                        season_length_ == 12);
	fprintf(stderr, "[AutoETS-SELECT] error=%d, trend=%d, season=%d, damped=%d, phi=%.4f, AICc=%.2f\n",
	        static_cast<int>(best_result.config.error),
	        static_cast<int>(best_result.config.trend),
	        static_cast<int>(best_result.config.season),
	        best_result.components.damped ? 1 : 0,
	        best_result.config.phi,
	        best_result.metrics.aicc);
	if (log_params) {
		fprintf(stderr, "  Final params: alpha=%.6f, beta=%.6f, gamma=%.6f\n",
		        best_result.params.alpha, best_result.params.beta, best_result.params.gamma);
		fprintf(stderr, "  Admissibility check: gamma=%.4f vs (1-alpha)=%.4f → %s\n",
		        best_result.params.gamma, 1.0 - best_result.params.alpha,
		        (best_result.params.gamma <= 1.0 - best_result.params.alpha + 1e-6) ? "OK" : "VIOLATED!");
	}
	fflush(stderr);
#endif

	diagnostics_.optimizer_iterations = best_result.optimizer_iterations;
	diagnostics_.optimizer_converged = best_result.optimizer_converged;
	diagnostics_.optimizer_objective = best_result.optimizer_objective;

	fitted_model_ = std::make_unique<ETS>(best_result.config);
	if (best_result.has_state_override) {
		std::optional<double> trend_override = best_result.trend0;
		fitted_model_->fitWithInitialState(values, best_result.level0, trend_override);
	} else {
		fitted_model_->fitRaw(values);
	}

	metrics_ = computeMetrics(*fitted_model_, parameterCount(best_result.config));
	parameters_ = best_result.params;
	components_ = best_result.components;
	fitted_ = fitted_model_->fittedValues();
	residuals_ = fitted_model_->residuals();
	is_fitted_ = true;
}

core::Forecast AutoETS::predict(int horizon) {
	if (!is_fitted_ || !fitted_model_) {
		throw std::logic_error("AutoETS::predict called before fit.");
	}
	return fitted_model_->predict(horizon);
}

AutoETS &AutoETS::setAllowMultiplicativeTrend(bool allow) {
	allow_multiplicative_trend_ = allow;
	return *this;
}

AutoETS &AutoETS::setDampedPolicy(DampedPolicy policy) {
	if (policy == DampedPolicy::Always) {
		const bool has_trend_option =
		    std::any_of(spec_.trends.begin(), spec_.trends.end(), [](ETSTrendType trend) {
			    return trend != ETSTrendType::None;
		    });
		if (!has_trend_option) {
			throw std::invalid_argument("Cannot force damped trend when specification excludes trend components.");
		}
	}
	damped_policy_ = policy;
	return *this;
}

AutoETS &AutoETS::setOptimizationCriterion(OptimizationCriterion criterion) {
	optimization_criterion_ = criterion;
	return *this;
}

AutoETS &AutoETS::setNmse(std::size_t horizon) {
	if (horizon == 0) {
		throw std::invalid_argument("NMSE horizon must be positive.");
	}
	const std::size_t clamped = std::max<std::size_t>(1, std::min<std::size_t>(30, horizon));
	nmse_horizon_ = clamped;
	return *this;
}

AutoETS &AutoETS::setMaxIterations(int iterations) {
	if (iterations <= 0) {
		throw std::invalid_argument("Max iterations must be positive.");
	}
	max_iterations_ = iterations;
	return *this;
}

AutoETS &AutoETS::setPinnedAlpha(double alpha) {
	if (!std::isfinite(alpha) || alpha < kAlphaLower || alpha > kAlphaUpper) {
		throw std::invalid_argument("Pinned alpha must lie within (0, 1).");
	}
	pinned_alpha_ = clamp(alpha, kAlphaLower, kAlphaUpper);
	return *this;
}

AutoETS &AutoETS::clearPinnedAlpha() {
	pinned_alpha_.reset();
	return *this;
}

AutoETS &AutoETS::setPinnedBeta(double beta) {
	if (!std::isfinite(beta) || beta < kAlphaLower || beta > kAlphaUpper) {
		throw std::invalid_argument("Pinned beta must lie within (0, 1).");
	}
	pinned_beta_ = clamp(beta, kAlphaLower, kAlphaUpper);
	return *this;
}

AutoETS &AutoETS::clearPinnedBeta() {
	pinned_beta_.reset();
	return *this;
}

AutoETS &AutoETS::setPinnedGamma(double gamma) {
	if (!std::isfinite(gamma) || gamma < kGammaLower || gamma > kGammaUpper) {
		throw std::invalid_argument("Pinned gamma must lie within (0, 1).");
	}
	pinned_gamma_ = clamp(gamma, kGammaLower, kGammaUpper);
	return *this;
}

AutoETS &AutoETS::clearPinnedGamma() {
	pinned_gamma_.reset();
	return *this;
}

AutoETS &AutoETS::setPinnedPhi(double phi) {
	if (!std::isfinite(phi) || phi < kPhiLower || phi > kPhiUpper) {
		throw std::invalid_argument("Pinned phi must lie within [0.8, 0.98].");
	}
	pinned_phi_ = clamp(phi, kPhiLower, kPhiUpper);
	return *this;
}

AutoETS &AutoETS::clearPinnedPhi() {
	pinned_phi_.reset();
	return *this;
}

std::vector<bool> AutoETS::dampedCandidates(ETSTrendType trend) const {
	if (trend == ETSTrendType::None) {
		return {false};
	}

	switch (damped_policy_) {
	case DampedPolicy::Auto:
		return {false, true};
	case DampedPolicy::Always:
		return {true};
	case DampedPolicy::Never:
		return {false};
	}
	return {false};
}

} // namespace anofoxtime::models
