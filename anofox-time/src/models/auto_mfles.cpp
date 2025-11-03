#include "anofox-time/models/auto_mfles.hpp"
#include <chrono>
#include <algorithm>

namespace anofoxtime::models {

AutoMFLES::AutoMFLES()
	: config_() {}

AutoMFLES::AutoMFLES(const Config& config)
	: config_(config) {}

void AutoMFLES::fit(const core::TimeSeries& ts) {
	auto start = std::chrono::high_resolution_clock::now();

	// Optimize hyperparameters using CV
	optimizeParameters(ts);

	// Fit final model on full dataset with best parameters
	MFLES::Params best_params;
	best_params.seasonal_periods = config_.seasonal_periods;
	best_params.fourier_order = best_fourier_order_;
	best_params.max_rounds = best_max_rounds_;
	best_params.trend_method = best_trend_method_;
	best_params.min_alpha = config_.min_alpha;
	best_params.max_alpha = config_.max_alpha;
	best_params.es_ensemble_steps = config_.es_ensemble_size;

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
	// Generate candidate configurations
	auto candidates = generateCandidates();

	diagnostics_.configs_evaluated = static_cast<int>(candidates.size());

	// Evaluate each candidate using CV
	for (auto& candidate : candidates) {
		MFLES::Params params;
		params.seasonal_periods = config_.seasonal_periods;
		params.fourier_order = candidate.fourier_order;
		params.max_rounds = candidate.max_rounds;
		params.trend_method = candidate.trend_method;
		params.min_alpha = config_.min_alpha;
		params.max_alpha = config_.max_alpha;
		params.es_ensemble_steps = config_.es_ensemble_size;

		candidate.cv_mae = evaluateConfig(ts, params);
	}

	// Select best configuration (lowest CV MAE)
	auto best_it = std::min_element(candidates.begin(), candidates.end());

	if (best_it != candidates.end()) {
		best_trend_method_ = best_it->trend_method;
		best_fourier_order_ = best_it->fourier_order;
		best_max_rounds_ = best_it->max_rounds;
		best_cv_mae_ = best_it->cv_mae;

		// Update diagnostics
		diagnostics_.best_trend_method = best_trend_method_;
		diagnostics_.best_fourier_order = best_fourier_order_;
		diagnostics_.best_max_rounds = best_max_rounds_;
		diagnostics_.best_cv_mae = best_cv_mae_;
	}
}

double AutoMFLES::evaluateConfig(const core::TimeSeries& ts, const MFLES::Params& params) {
	// Configure CV
	utils::CVConfig cv_config;
	cv_config.horizon = config_.cv_horizon;
	cv_config.initial_window = config_.cv_initial_window;
	cv_config.step = config_.cv_step;
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

	// Grid search over all combinations
	for (const auto& trend_method : config_.trend_methods) {
		for (int fourier_order : config_.max_fourier_orders) {
			for (int max_rounds : config_.max_rounds_options) {
				CandidateConfig config;
				config.trend_method = trend_method;
				config.fourier_order = fourier_order;
				config.max_rounds = max_rounds;
				config.cv_mae = std::numeric_limits<double>::infinity();
				candidates.push_back(config);
			}
		}
	}

	return candidates;
}

} // namespace anofoxtime::models
