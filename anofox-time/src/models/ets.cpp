#include "anofox-time/models/ets.hpp"
#include "anofox-time/core/time_series.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <iostream>

// Forward declarations of statsforecast ETS core functions
namespace anofoxtime::models {
extern std::pair<double, double> ets_update_statsforecast(
    std::vector<double>& s, double old_l, double old_b,
    const std::vector<double>& old_s, int m,
    bool has_trend, bool trend_additive,
    bool has_season, bool season_additive,
    double alpha, double beta, double gamma, double phi, double y);

extern void ets_forecast_statsforecast(
    std::vector<double>& forecast, double l, double b,
    const std::vector<double>& s, int m,
    bool has_trend, bool trend_additive,
    bool has_season, bool season_additive,
    double phi, int h);
}

namespace {
constexpr double kEpsilon = 1e-8;
constexpr double kPositiveFloor = 1e-6;
constexpr double kSeasonalRatioLower = 0.01;  // For multiplicative seasonals (1% to 100x)
constexpr double kSeasonalRatioUpper = 100.0;  // Allow wide range, optimizer will constrain
constexpr double kTrendRatioLower = 0.01;
constexpr double kTrendRatioUpper = 10.0;

inline double clampNonZero(double value) {
	if (std::abs(value) < kEpsilon) {
		return (value >= 0.0) ? kEpsilon : -kEpsilon;
	}
	return value;
}

inline double safeDivide(double numerator, double denominator) {
	return numerator / clampNonZero(denominator);
}

inline double clamp(double value, double lower, double upper) {
	return std::max(lower, std::min(value, upper));
}

inline double clampPositive(double value) {
	return std::max(value, kPositiveFloor);
}

inline double clampSeasonalRatio(double value) {
	// For multiplicative seasonality, clamp to reasonable ratio range
	return clamp(value, kSeasonalRatioLower, kSeasonalRatioUpper);
}

} // namespace

namespace anofoxtime::models {

ETS::ETS(ETSConfig config) : config_(std::move(config)) {
	validateConfig();
}

void ETS::validateConfig() const {
	if (config_.alpha <= 0.0 || config_.alpha > 1.0) {
		throw std::invalid_argument("ETS alpha must be in (0, 1].");
	}
	const bool trend_enabled = config_.trend != ETSTrendType::None;
	const bool trend_damped = config_.trend == ETSTrendType::DampedAdditive ||
	                          config_.trend == ETSTrendType::DampedMultiplicative;
	const bool trend_multiplicative = config_.trend == ETSTrendType::Multiplicative ||
	                                  config_.trend == ETSTrendType::DampedMultiplicative;

	if (trend_enabled) {
		if (!config_.beta.has_value()) {
			throw std::invalid_argument(
			    "ETS beta smoothing parameter required when trend component is enabled.");
		}
		if (*config_.beta <= 0.0 || *config_.beta > 1.0) {
			throw std::invalid_argument("ETS beta must be in (0, 1].");
		}
		if (trend_damped && (config_.phi <= 0.0 || config_.phi > 1.0)) {
			throw std::invalid_argument("ETS damped trend requires phi in (0, 1].");
		}
		if (!trend_damped && config_.phi <= 0.0) {
			// Ensure phi is at least unity for non-damped configurations.
			throw std::invalid_argument("ETS non-damped trend requires phi > 0.");
		}
		if (trend_multiplicative && config_.beta.value() <= 0.0) {
			throw std::invalid_argument("ETS multiplicative trend requires positive beta.");
		}
	}

	if (config_.season != ETSSeasonType::None) {
		if (!config_.gamma.has_value()) {
			throw std::invalid_argument(
			    "ETS gamma smoothing parameter required when seasonality is enabled.");
		}
		if (*config_.gamma <= 0.0 || *config_.gamma > 1.0) {
			throw std::invalid_argument("ETS gamma must be in (0, 1].");
		}
		if (config_.season_length < 2) {
			throw std::invalid_argument("ETS season length must be at least 2 when seasonality is enabled.");
		}
	}

	if (config_.error == ETSErrorType::Multiplicative && config_.season == ETSSeasonType::Additive) {
		throw std::invalid_argument("Multiplicative error with additive seasonality is not supported.");
	}
}

void ETS::initializeStates(const std::vector<double> &values) {
	// PORT OF STATSFORECAST'S initstate() - ets_python.txt lines 264-339
	const std::size_t n = values.size();
	int m = std::max(1, config_.season_length);
	
	// Debug disabled for performance
	
	std::vector<double> init_seas;
	std::vector<double> y_sa = values;  // Seasonally adjusted data
	
	// Seasonal initialization
	if (config_.season != ETSSeasonType::None) {
		if (n < 4) {
			throw std::invalid_argument("Not enough data for seasonal ETS model");
		}
		
		if (n < 3 * static_cast<std::size_t>(m)) {
			// Small dataset: simple seasonal averaging
			std::vector<double> season_sums(m, 0.0);
			std::vector<int> season_counts(m, 0);
			
			for (std::size_t i = 0; i < n; ++i) {
				season_sums[i % m] += values[i];
				season_counts[i % m]++;
			}
			
		// Compute average for each season
		std::vector<double> season_avg(m);
		for (int j = 0; j < m; ++j) {
			if (season_counts[j] > 0) {
				season_avg[j] = season_sums[j] / static_cast<double>(season_counts[j]);
			} else {
				season_avg[j] = config_.season == ETSSeasonType::Additive ? 0.0 : 1.0;
			}
		}
		
		// Set up rotating buffer: old_s[m-1-j] = season_avg[j]
		init_seas.resize(m);
		for (int j = 0; j < m; ++j) {
			init_seas[m - 1 - j] = season_avg[j];
		}
			
		// Seasonally adjust using season_avg (not init_seas which is rotated)
		if (config_.season == ETSSeasonType::Additive) {
			for (std::size_t i = 0; i < n; ++i) {
				y_sa[i] = values[i] - season_avg[i % m];
			}
		} else {
			for (std::size_t i = 0; i < n; ++i) {
				y_sa[i] = values[i] / std::max(season_avg[i % m], 0.01);
			}
		}
		} else {
			// Large dataset: use seasonal decomposition
			// Simple centered MA decomposition
			std::vector<double> trend(n, 0.0);
			const int half = m / 2;
			
			for (std::size_t i = m; i < n - m; ++i) {
				double sum = 0.0;
				if (m % 2 == 0) {
					sum = 0.5 * values[i - half];
					for (int j = 1 - half; j < half; ++j) {
						sum += values[i + j];
					}
					sum += 0.5 * values[i + half];
				} else {
					for (int j = -half; j <= half; ++j) {
						sum += values[i + j];
					}
				}
				trend[i] = sum / static_cast<double>(m);
			}
			
			// Extract seasonal component
			std::vector<std::vector<double>> season_obs(m);
			for (std::size_t i = m; i < n - m; ++i) {
				if (trend[i] > 1e-10) {
					double deseas = (config_.season == ETSSeasonType::Additive) ?
					               (values[i] - trend[i]) : (values[i] / trend[i]);
					season_obs[i % m].push_back(deseas);
				}
			}
			
			// Average seasonal values
			std::vector<double> season_avg(m);
			for (int j = 0; j < m; ++j) {
				if (!season_obs[j].empty()) {
					double sum = std::accumulate(season_obs[j].begin(), season_obs[j].end(), 0.0);
					season_avg[j] = sum / static_cast<double>(season_obs[j].size());
				} else {
					season_avg[j] = config_.season == ETSSeasonType::Additive ? 0.0 : 1.0;
				}
			}
			
			// Normalize: additive sums to 0, multiplicative averages to 1
			if (config_.season == ETSSeasonType::Additive) {
				double sum = std::accumulate(season_avg.begin(), season_avg.end(), 0.0);
				double correction = sum / static_cast<double>(m);
				for (double& s : season_avg) {
					s -= correction;
				}
			} else {
				double product_sum = std::accumulate(season_avg.begin(), season_avg.end(), 0.0);
				double avg = product_sum / static_cast<double>(m);
				if (avg > 1e-10) {
					for (double& s : season_avg) {
						s /= avg;
					}
				}
			}
			
		// Set up rotating buffer for statsforecast Update
		// At time t=0, Update uses old_s[m-1] which should be season_avg[0]
		// At time t=1, after rotation, Update uses old_s[m-1] which should be season_avg[1]
		// Working backwards: old_s[m-1-j] = season_avg[j]
		init_seas.resize(m);
		for (int j = 0; j < m; ++j) {
			init_seas[m - 1 - j] = season_avg[j];
		}
			
			// Seasonally adjust
			if (config_.season == ETSSeasonType::Additive) {
				for (std::size_t i = 0; i < n; ++i) {
					y_sa[i] = values[i] - season_avg[i % m];
				}
			} else {
				// Clip seasonal for stability
				for (double& s : init_seas) {
					s = std::max(s, 0.01);
				}
				for (std::size_t i = 0; i < n; ++i) {
					y_sa[i] = values[i] / std::max(season_avg[i % m], 0.01);
				}
			}
		}
	} else {
		m = 1;
	}
	
	// Initialize level and trend from seasonally-adjusted data
	// statsforecast: maxn = min(max(10, 2*m), len(y_sa))
	const int maxn_int = std::max(10, 2 * m);
	const size_t maxn = std::min(static_cast<size_t>(maxn_int), y_sa.size());
	
	const bool has_trend = (config_.trend != ETSTrendType::None);
	const bool trend_additive = (config_.trend == ETSTrendType::Additive || 
	                             config_.trend == ETSTrendType::DampedAdditive);
	const bool trend_multiplicative = (config_.trend == ETSTrendType::Multiplicative ||
	                                    config_.trend == ETSTrendType::DampedMultiplicative);
	
	if (!has_trend) {
		// No trend: level = mean of first maxn seasonally-adjusted points
		double sum = std::accumulate(y_sa.begin(), y_sa.begin() + maxn, 0.0);
		level_ = sum / static_cast<double>(maxn);
	} else {
		// Linear regression on first maxn points of y_sa
		double sum_x = 0.0, sum_y = 0.0, sum_xy = 0.0, sum_x2 = 0.0;
		for (std::size_t i = 0; i < maxn; ++i) {
			double x = static_cast<double>(i + 1);  // 1-indexed
			double y_val = y_sa[i];
			sum_x += x;
			sum_y += y_val;
			sum_xy += x * y_val;
			sum_x2 += x * x;
		}
		
		const double n_double = static_cast<double>(maxn);
		const double denom = n_double * sum_x2 - sum_x * sum_x;
		double l, b;
		
		if (std::abs(denom) > 1e-10) {
			b = (n_double * sum_xy - sum_x * sum_y) / denom;
			l = (sum_y - b * sum_x) / n_double;
		} else {
			l = sum_y / n_double;
			b = 0.0;
		}
		
		// Apply statsforecast's trend initialization logic
		if (trend_additive) {
			level_ = l;
			trend_ = b;
			
			// Perturb if too close to zero (for multiplicative error)
			if (std::abs(level_ + trend_) < 1e-8) {
				level_ *= (1.0 + 1e-3);
				trend_ *= (1.0 - 1e-3);
			}
		} else if (trend_multiplicative) {
			// Multiplicative trend initialization (complex)
			level_ = l + b;
			if (std::abs(level_) < 1e-8) {
				level_ = 1e-7;
			}
			trend_ = (l + 2.0 * b) / level_;
			
			double div = (std::abs(trend_) > 1e-8) ? trend_ : 1e-8;
			level_ = level_ / div;
			
			if (std::abs(trend_) > 1e10) {
				trend_ = std::copysign(1e10, trend_);
			}
			if (level_ < 1e-8 || trend_ < 1e-8) {
				// Fallback
				level_ = std::max(y_sa[0], 1e-3);
				trend_ = std::max(y_sa[1] / std::max(y_sa[0], 1e-8), 1e-3);
			}
		}
	}
	
	// Initialize seasonal components
	if (config_.season != ETSSeasonType::None) {
		seasonals_ = init_seas;
		last_season_index_ = (n - 1) % m;
	} else {
		seasonals_.clear();
		last_season_index_ = 0;
	}
}

void ETS::fitRaw(const std::vector<double> &values) {
	fitInternal(values, std::nullopt, std::nullopt);
}

void ETS::fitWithInitialState(const std::vector<double> &values,
                              std::optional<double> level0,
                              std::optional<double> trend0) {
	fitInternal(values, level0, trend0);
}

void ETS::fitWithFullState(const std::vector<double> &values,
                          std::optional<double> level0,
                          std::optional<double> trend0,
                          const std::vector<double> &seasonal0) {
	// Pass seasonal states to fitInternal
	fitInternal(values, level0, trend0, &seasonal0);
}

void ETS::fit(const core::TimeSeries &ts) {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("ETS currently supports univariate series only.");
	}
	fitInternal(ts.getValues(), std::nullopt, std::nullopt);
}

void ETS::fitInternal(const std::vector<double> &values,
                      std::optional<double> level_override,
                      std::optional<double> trend_override,
                      const std::vector<double> *seasonal_override) {
	if (values.empty()) {
		throw std::invalid_argument("ETS cannot fit an empty time series.");
	}
	const bool trend_multiplicative = config_.trend == ETSTrendType::Multiplicative ||
	                                  config_.trend == ETSTrendType::DampedMultiplicative;
	const bool season_multiplicative = config_.season == ETSSeasonType::Multiplicative;
	const bool requires_positive = (config_.error == ETSErrorType::Multiplicative) || trend_multiplicative ||
	                               season_multiplicative;

	if (requires_positive) {
		for (double v : values) {
			if (v <= 0.0) {
				throw std::invalid_argument("Multiplicative configurations require strictly positive observations.");
			}
		}
	}

	initializeStates(values);
	if (level_override) {
		level_ = requires_positive ? clampPositive(*level_override) : *level_override;
	}
	if (trend_override && config_.trend != ETSTrendType::None) {
		if (trend_multiplicative) {
			trend_ = clamp(*trend_override, kTrendRatioLower, kTrendRatioUpper);
		} else {
			trend_ = *trend_override;
		}
	}
	if (seasonal_override && !seasonal_override->empty()) {
		int m = config_.season_length;
		if (m > 0 && static_cast<int>(seasonal_override->size()) == m) {
			seasonals_ = *seasonal_override;
		}
	}

	fitted_.clear();
	residuals_.clear();
	fitted_.reserve(values.size());
	residuals_.reserve(values.size());

	innovation_sse_ = 0.0;
	sse_ = 0.0;
	sum_log_forecast_ = 0.0;
	sample_size_ = values.size();
	log_likelihood_ = 0.0;

	const int m = seasonals_.empty() ? 1 : static_cast<int>(seasonals_.size());
	
	// statsforecast uses rotating seasonal buffer: s[0]=newest, s[m-1]=oldest
	std::vector<double> old_s = seasonals_;
	std::vector<double> new_s(m);
	
	// Prepare parameters for statsforecast functions
	const bool has_trend = (config_.trend != ETSTrendType::None);
	const bool trend_additive = (config_.trend == ETSTrendType::Additive || 
	                             config_.trend == ETSTrendType::DampedAdditive);
	const bool has_season = (config_.season != ETSSeasonType::None);
	const bool season_additive = (config_.season == ETSSeasonType::Additive);
	
	const double alpha = config_.alpha;
	const double beta = config_.beta.value_or(0.0);
	const double gamma = config_.gamma.value_or(0.0);
	const double phi = config_.phi;
	
	// Debug disabled for performance
	const bool debug_fit = false;
	
	// Main fitting loop using exact statsforecast logic
	for (std::size_t t = 0; t < values.size(); ++t) {
		const double observation = values[t];
		
		// Step 1: Compute one-step forecast using statsforecast Forecast
		std::vector<double> one_step_forecast;
		ets_forecast_statsforecast(one_step_forecast, level_, trend_, old_s, m,
		                           has_trend, trend_additive, has_season, season_additive, phi, 1);
		
		const double fitted = (one_step_forecast.empty()) ? level_ : one_step_forecast[0];
		
		if (debug_fit && t < 5) {
			fprintf(stderr, "[ETS-FIT-t=%zu] y=%.2f, fitted=%.2f, l_before=%.2f, b_before=%.4f\n",
			        t, observation, fitted, level_, trend_);
			fflush(stderr);
		}
		
		// Step 2: Compute innovation (error)
		double innovation = 0.0;
		if (config_.error == ETSErrorType::Additive) {
			innovation = observation - fitted;
		} else {
			// Multiplicative error
			innovation = (fitted > 1e-10) ? (observation / fitted - 1.0) : 0.0;
		}
		
		// Step 3: Track fitted values and residuals
		const double residual = observation - fitted;
		fitted_.push_back(fitted);
		residuals_.push_back(residual);
		
		innovation_sse_ += innovation * innovation;
		sse_ += residual * residual;
		if (config_.error == ETSErrorType::Multiplicative) {
			sum_log_forecast_ += std::log(std::max(std::abs(fitted), 1e-10));
		}
		
		// Step 4: Update states using exact statsforecast Update function
		auto [new_level, new_trend] = ets_update_statsforecast(
			new_s, level_, trend_, old_s, m,
			has_trend, trend_additive, has_season, season_additive,
			alpha, beta, gamma, phi, observation);
		
		if (debug_fit && t < 5) {
			fprintf(stderr, "  → new_l=%.2f, new_b=%.4f, innovation=%.2f\n",
			        new_level, new_trend, innovation);
			fflush(stderr);
		}
		
		// Step 5: Update internal state
		level_ = new_level;
		trend_ = new_trend;
		
		// Step 6: Swap seasonal buffers (rotating buffer)
		if (has_season) {
			old_s = new_s;
		}
	}

	// Store final seasonal state (rotating buffer -> seasonals_)
	if (has_season) {
		seasonals_ = old_s;
	}
	
	const double n = static_cast<double>(sample_size_);
	double lik = innovation_sse_ > 0.0 ? n * std::log(innovation_sse_) : n * std::log(kEpsilon);
	if (config_.error == ETSErrorType::Multiplicative) {
		lik += 2.0 * sum_log_forecast_;
	}
	log_likelihood_ = -0.5 * lik;
	mse_ = (sample_size_ > 0) ? sse_ / n : 0.0;
	is_fitted_ = true;
	
	// Debug disabled for performance
}

core::Forecast ETS::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("ETS::predict called before fit.");
	}
	if (horizon <= 0) {
		return {};
	}

	const int m = seasonals_.empty() ? 1 : static_cast<int>(seasonals_.size());
	
	// Prepare parameters for statsforecast functions
	const bool has_trend = (config_.trend != ETSTrendType::None);
	const bool trend_additive = (config_.trend == ETSTrendType::Additive || 
	                             config_.trend == ETSTrendType::DampedAdditive);
	const bool has_season = (config_.season != ETSSeasonType::None);
	const bool season_additive = (config_.season == ETSSeasonType::Additive);
	
	// Debug: disabled
	const bool debug_damped_seasonal = false;
	
	// Use exact statsforecast Forecast function
	std::vector<double> forecast_values;
	ets_forecast_statsforecast(forecast_values, level_, trend_, seasonals_, m,
	                           has_trend, trend_additive, has_season, season_additive,
	                           config_.phi, horizon);
	
	core::Forecast forecast;
	auto &series = forecast.primary();
	series = forecast_values;
	
	return forecast;
}

double ETS::computeForecastComponent(double horizon_step) const {
	if (config_.trend == ETSTrendType::None) {
		return level_;
	}

	if (config_.trend == ETSTrendType::Additive) {
		return level_ + horizon_step * trend_;
	}

	if (config_.trend == ETSTrendType::DampedAdditive) {
		if (std::abs(config_.phi - 1.0) < kEpsilon) {
			return level_ + horizon_step * trend_;
		}
		const double phi_pow = std::pow(config_.phi, horizon_step);
		const double numerator = config_.phi * (1.0 - phi_pow);
		double multiplier = numerator / (1.0 - config_.phi);
		return level_ + multiplier * trend_;
	}

	if (config_.trend == ETSTrendType::Multiplicative) {
		const double safe_trend = clamp(trend_, kTrendRatioLower, kTrendRatioUpper);
		return level_ * std::pow(safe_trend, horizon_step);
	}

	if (config_.trend == ETSTrendType::DampedMultiplicative) {
		const double safe_trend = clamp(trend_, kTrendRatioLower, kTrendRatioUpper);
		if (std::abs(config_.phi - 1.0) < kEpsilon) {
			return level_ * std::pow(safe_trend, horizon_step);
		}
		const double phi_pow = std::pow(config_.phi, horizon_step);
		const double phi_sum = (config_.phi * (1.0 - phi_pow)) / (1.0 - config_.phi);
		return level_ * std::pow(safe_trend, phi_sum);
	}

	return level_;
}

double ETS::aic(std::size_t parameter_count) const {
	const double neg2loglik = -2.0 * log_likelihood_;
	if (!std::isfinite(neg2loglik)) {
		return std::numeric_limits<double>::infinity();
	}
	return neg2loglik + 2.0 * static_cast<double>(parameter_count);
}

double ETS::aicc(std::size_t parameter_count) const {
	const double aic_value = aic(parameter_count);
	if (!std::isfinite(aic_value)) {
		return aic_value;
	}
	const double n = static_cast<double>(sample_size_);
	if (n <= static_cast<double>(parameter_count) + 1.0) {
		return std::numeric_limits<double>::infinity();
	}
	return aic_value + (2.0 * static_cast<double>(parameter_count) * (static_cast<double>(parameter_count) + 1.0)) /
	                     (n - static_cast<double>(parameter_count) - 1.0);
}

} // namespace anofoxtime::models
