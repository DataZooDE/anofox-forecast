#include "anofox-time/models/auto_tbats.hpp"
#include <algorithm>
#include <chrono>
#include <limits>

namespace anofoxtime::models {

AutoTBATS::AutoTBATS(std::vector<int> seasonal_periods)
	: seasonal_periods_(std::move(seasonal_periods))
{
	if (seasonal_periods_.empty()) {
		throw std::invalid_argument("AutoTBATS: seasonal_periods cannot be empty");
	}
	
	for (int period : seasonal_periods_) {
		if (period < 2) {
			throw std::invalid_argument("AutoTBATS: all seasonal periods must be >= 2");
		}
	}
}

void AutoTBATS::fit(const core::TimeSeries& ts) {
	auto start_time = std::chrono::high_resolution_clock::now();
	
	// Optimize parameters
	optimizeParameters(ts);
	
	auto end_time = std::chrono::high_resolution_clock::now();
	diagnostics_.optimization_time_ms = 
		std::chrono::duration<double, std::milli>(end_time - start_time).count();
}

core::Forecast AutoTBATS::predict(int horizon) {
	if (!fitted_model_) {
		throw std::runtime_error("AutoTBATS: Must call fit() before predict()");
	}
	
	return fitted_model_->predict(horizon);
}

std::vector<TBATS::Config> AutoTBATS::generateCandidates() {
	std::vector<TBATS::Config> candidates;
	
	// Box-Cox configurations
	std::vector<std::pair<bool, double>> boxcox_configs = {
		{false, 1.0},  // No transformation
		{true, 0.0},   // Log transformation
		{true, 0.5},   // Square root
		{true, 1.0}    // Linear (equivalent to no transform)
	};
	
	// Trend configurations
	std::vector<std::tuple<bool, bool>> trend_configs = {
		{false, false},  // No trend
		{true, false},   // Linear trend
		{true, true}     // Damped trend
	};
	
	// ARMA configurations (limited for speed)
	std::vector<std::pair<int, int>> arma_configs = {
		{0, 0},  // No ARMA
		{1, 0},  // AR(1)
		{0, 1},  // MA(1)
		{1, 1}   // ARMA(1,1)
	};
	
	// Generate all combinations
	for (const auto& [use_bc, lambda] : boxcox_configs) {
		for (const auto& [use_trend, use_damped] : trend_configs) {
			for (const auto& [ar, ma] : arma_configs) {
				TBATS::Config config;
				config.seasonal_periods = seasonal_periods_;
				config.use_box_cox = use_bc;
				config.box_cox_lambda = lambda;
				config.use_trend = use_trend;
				config.use_damped_trend = use_damped;
				config.ar_order = ar;
				config.ma_order = ma;
				// Fourier K will be auto-selected during fit
				
				candidates.push_back(config);
			}
		}
	}
	
	return candidates;
}

double AutoTBATS::computeAIC(const TBATS& model) {
	return model.aic();
}

void AutoTBATS::optimizeParameters(const core::TimeSeries& ts) {
	auto candidates = generateCandidates();
	diagnostics_.models_evaluated = 0;
	
	double best_aic = std::numeric_limits<double>::infinity();
	TBATS::Config best_config;
	
	// Evaluate each candidate
	for (const auto& config : candidates) {
		try {
			TBATS model(config);
			model.fit(ts);
			
			double aic = model.aic();
			diagnostics_.models_evaluated++;
			
			if (aic < best_aic) {
				best_aic = aic;
				best_config = config;
				// Store the fitted model
				fitted_model_ = std::make_unique<TBATS>(config);
				fitted_model_->fit(ts);
			}
			
		} catch (const std::exception&) {
			// Model failed to fit, skip it
			continue;
		}
	}
	
	if (!fitted_model_) {
		throw std::runtime_error("AutoTBATS: Failed to fit any valid model");
	}
	
	// Update diagnostics
	diagnostics_.best_aic = best_aic;
	diagnostics_.best_config = best_config;
}

} // namespace anofoxtime::models

