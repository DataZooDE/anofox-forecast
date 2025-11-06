#include "anofox-time/models/auto_mfles.hpp"
#include <chrono>
#include <algorithm>

namespace anofoxtime::models {

AutoMFLES::AutoMFLES()
	: config_() {
}

AutoMFLES::AutoMFLES(const Config& config)
	: config_(config) {
}

void AutoMFLES::fit(const core::TimeSeries& ts) {
	auto start = std::chrono::high_resolution_clock::now();

	// Optimize hyperparameters using CV (statsforecast grid)
	optimizeParameters(ts);

	// Fit final model on full dataset with best parameters
	MFLES::Params best_params;

	// Fixed parameters (not optimized)
	best_params.max_rounds = config_.max_rounds;
	best_params.trend_method = config_.trend_method;
	best_params.fourier_order = config_.fourier_order;
	best_params.min_alpha = config_.min_alpha;
	best_params.max_alpha = config_.max_alpha;
	best_params.es_ensemble_steps = config_.es_ensemble_size;

	// Learning rates (configurable)
	best_params.lr_trend = config_.lr_trend;
	best_params.lr_season = config_.lr_season;
	best_params.lr_rs = config_.lr_rs;

	// Statsforecast optimized parameters
	best_params.seasonality_weights = best_seasonality_weights_;
	best_params.smoother = best_smoother_;

	// Handle seasonal_period option (None vs period)
	if (best_seasonal_period_) {
		best_params.seasonal_periods = config_.seasonal_periods;
	} else {
		best_params.seasonal_periods = {};  // No seasonality
	}

	// Handle ma_window option (-1=period, -2=period/2, -3=None/ES)
	if (best_ma_window_ == -3) {
		// -3 = No smoother, use ES ensemble
		best_params.smoother = false;
	} else if (!best_smoother_) {
		// CV explicitly selected smoother=false (ES ensemble)
		best_params.smoother = false;
	} else {
		// CV selected smoother=true, translate ma_window
		best_params.smoother = true;
		if (best_ma_window_ == -1) {
			best_params.ma_window = config_.seasonal_periods.empty() ? 5 : config_.seasonal_periods[0];
		} else if (best_ma_window_ == -2) {
			best_params.ma_window = config_.seasonal_periods.empty() ? 3 : config_.seasonal_periods[0] / 2;
		} else {
			// Explicit ma_window value
			best_params.ma_window = best_ma_window_;
		}
	}

	fitted_model_ = std::make_unique<MFLES>(best_params);
	fitted_model_->fit(ts);

	auto end = std::chrono::high_resolution_clock::now();
	diagnostics_.optimization_time_ms =
		std::chrono::duration<double, std::milli>(end - start).count();
}

core::Forecast AutoMFLES::predict(int horizon) {
	if (!fitted_model_) {
		throw std::runtime_error("AutoMFLES: Must call fit() before predict()");
	}
	return fitted_model_->predict(horizon);
}

void AutoMFLES::optimizeParameters(const core::TimeSeries& ts) {
	// Generate candidate configurations (statsforecast grid: 24 configs)
	auto candidates = generateCandidates();

	diagnostics_.configs_evaluated = static_cast<int>(candidates.size());

	// Evaluate each candidate using CV
	for (auto& candidate : candidates) {
		MFLES::Params params;

		// Fixed parameters
		params.max_rounds = config_.max_rounds;
		params.trend_method = config_.trend_method;
		params.fourier_order = config_.fourier_order;
		params.min_alpha = config_.min_alpha;
		params.max_alpha = config_.max_alpha;
		params.es_ensemble_steps = config_.es_ensemble_size;

		// Learning rates (configurable)
		params.lr_trend = config_.lr_trend;
		params.lr_season = config_.lr_season;
		params.lr_rs = config_.lr_rs;

		// Statsforecast grid parameters
		params.seasonality_weights = candidate.seasonality_weights;
		params.smoother = candidate.smoother;

		// Handle seasonal_period
		if (candidate.seasonal_period) {
			params.seasonal_periods = config_.seasonal_periods;
		} else {
			params.seasonal_periods = {};
		}

		// Handle ma_window
		if (candidate.ma_window == -3) {
			params.smoother = false;  // -3 = No smoother, use ES ensemble
		} else if (!candidate.smoother) {
			params.smoother = false;  // smoother=false means ES ensemble
		} else {
			// smoother=true, translate ma_window
			params.smoother = true;
			if (candidate.ma_window == -1) {
				params.ma_window = config_.seasonal_periods.empty() ? 5 : config_.seasonal_periods[0];
			} else if (candidate.ma_window == -2) {
				params.ma_window = config_.seasonal_periods.empty() ? 3 : config_.seasonal_periods[0] / 2;
			}
		}

		candidate.cv_mae = evaluateConfig(ts, params);
	}

	// Select best configuration (lowest CV MAE)
	auto best_it = std::min_element(candidates.begin(), candidates.end());

	if (best_it != candidates.end()) {
		best_seasonality_weights_ = best_it->seasonality_weights;
		best_smoother_ = best_it->smoother;
		best_ma_window_ = best_it->ma_window;
		best_seasonal_period_ = best_it->seasonal_period;
		best_cv_mae_ = best_it->cv_mae;

		// Update diagnostics
		diagnostics_.best_seasonality_weights = best_seasonality_weights_;
		diagnostics_.best_smoother = best_smoother_;
		diagnostics_.best_ma_window = best_ma_window_;
		diagnostics_.best_seasonal_period = best_seasonal_period_;
		diagnostics_.best_cv_mae = best_cv_mae_;
	}
}

double AutoMFLES::evaluateConfig(const core::TimeSeries& ts, const MFLES::Params& params) {
	// Configure CV with auto-detection (matching statsforecast approach)
	utils::CVConfig cv_config;

	// Auto-detect cv_horizon from seasonal_periods if set to -1
	// Statsforecast recommends test_size = season_length or season_length/2
	int cv_horizon = config_.cv_horizon;
	if (cv_horizon == -1) {
		cv_horizon = config_.seasonal_periods.empty() ? 7 : config_.seasonal_periods[0];
	}
	cv_config.horizon = cv_horizon;

	// Auto-detect initial_window: statsforecast doesn't specify, using 10x horizon as heuristic
	cv_config.initial_window = (config_.cv_initial_window == -1)
		? (10 * cv_horizon)
		: config_.cv_initial_window;

	// Auto-detect step: statsforecast uses n_windows to determine folds, step = horizon is standard
	cv_config.step = (config_.cv_step == -1)
		? cv_horizon
		: config_.cv_step;

	cv_config.strategy = config_.cv_strategy;

	// Model factory for this configuration
	auto model_factory = [&params]() -> std::unique_ptr<IForecaster> {
		return std::make_unique<MFLES>(params);
	};

	// Run CV
	try {
		auto cv_results = utils::CrossValidation::evaluate(ts, model_factory, cv_config);
		return cv_results.mae;  // Use MAE as optimization criterion
	} catch (const std::exception&) {
		// If CV fails, return a very large MAE
		return std::numeric_limits<double>::infinity();
	}
}

std::vector<AutoMFLES::CandidateConfig> AutoMFLES::generateCandidates() const {
	std::vector<CandidateConfig> candidates;

	// Statsforecast grid search: 2×2×3×2 = 24 configurations
	// Parameters: seasonality_weights, smoother, ma_window, seasonal_period

	for (bool seasonality_weights : config_.seasonality_weights_options) {
		for (bool smoother : config_.smoother_options) {
			for (int ma_window : config_.ma_window_options) {
				for (bool seasonal_period : config_.seasonal_period_options) {
					CandidateConfig config;
					config.seasonality_weights = seasonality_weights;
					config.smoother = smoother;
					config.ma_window = ma_window;
					config.seasonal_period = seasonal_period;
					config.cv_mae = std::numeric_limits<double>::infinity();
					candidates.push_back(config);
				}
			}
		}
	}

	return candidates;
}

} // namespace anofoxtime::models
