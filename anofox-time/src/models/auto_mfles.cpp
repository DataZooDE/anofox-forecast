#include "anofox-time/models/auto_mfles.hpp"
#include <cmath>
#include <algorithm>
#include <chrono>
#include <limits>

namespace anofoxtime::models {

AutoMFLES::AutoMFLES(std::vector<int> seasonal_periods, int test_size)
	: seasonal_periods_(std::move(seasonal_periods))
	, test_size_(test_size)
{
	if (seasonal_periods_.empty()) {
		throw std::invalid_argument("AutoMFLES: seasonal_periods cannot be empty");
	}
	
	for (int period : seasonal_periods_) {
		if (period < 1) {
			throw std::invalid_argument("AutoMFLES: all seasonal periods must be >= 1");
		}
	}
	
	if (test_size_ < 0) {
		throw std::invalid_argument("AutoMFLES: test_size must be >= 0");
	}
}

void AutoMFLES::fit(const core::TimeSeries& ts) {
	auto start_time = std::chrono::high_resolution_clock::now();
	
	// Optimize parameters
	optimizeParameters(ts);
	
	// Fit final model with best parameters
	fitted_model_ = std::make_unique<MFLES>(
		seasonal_periods_,
		best_iterations_,
		best_lr_trend_,
		best_lr_season_,
		best_lr_level_
	);
	fitted_model_->fit(ts);
	
	auto end_time = std::chrono::high_resolution_clock::now();
	diagnostics_.optimization_time_ms = 
		std::chrono::duration<double, std::milli>(end_time - start_time).count();
}

core::Forecast AutoMFLES::predict(int horizon) {
	if (!fitted_model_) {
		throw std::runtime_error("AutoMFLES: Must call fit() before predict()");
	}
	
	return fitted_model_->predict(horizon);
}

std::vector<AutoMFLES::CandidateConfig> AutoMFLES::generateCandidates() const {
	std::vector<CandidateConfig> candidates;
	
	// Define search space
	std::vector<int> iterations_grid = {1, 2, 3, 5};
	std::vector<double> lr_trend_grid = {0.2, 0.5, 0.8};
	std::vector<double> lr_season_grid = {0.2, 0.5, 0.8};
	std::vector<double> lr_level_grid = {0.5, 0.8};
	
	// Generate all combinations
	for (int iter : iterations_grid) {
		for (double lr_t : lr_trend_grid) {
			for (double lr_s : lr_season_grid) {
				for (double lr_l : lr_level_grid) {
					CandidateConfig config;
					config.iterations = iter;
					config.lr_trend = lr_t;
					config.lr_season = lr_s;
					config.lr_level = lr_l;
					config.aic = std::numeric_limits<double>::infinity();
					candidates.push_back(config);
				}
			}
		}
	}
	
	return candidates;
}

double AutoMFLES::computeAIC(const MFLES& model, int n, int k) const {
	// AIC = 2k + n*log(RSS/n)
	// where k = number of parameters, n = sample size, RSS = residual sum of squares
	
	const auto& residuals = model.residuals();
	
	if (residuals.empty() || n <= 0) {
		return std::numeric_limits<double>::infinity();
	}
	
	// Compute RSS
	double rss = 0.0;
	for (double r : residuals) {
		rss += r * r;
	}
	
	// Avoid log(0) or negative values
	if (rss <= 0.0 || rss / n <= 0.0) {
		return std::numeric_limits<double>::infinity();
	}
	
	// Number of parameters:
	// - Fourier coefficients: 2K per seasonal period
	// - Trend: 2 (slope + intercept)
	// - Level: 1
	int num_params = 3;  // Base: trend slope, intercept, level
	for (int period : seasonal_periods_) {
		int K = std::min(period / 2, 10);
		num_params += 2 * K;  // sin and cos coefficients
	}
	
	double aic = 2.0 * num_params + n * std::log(rss / n);
	
	return aic;
}

void AutoMFLES::optimizeParameters(const core::TimeSeries& ts) {
	auto candidates = generateCandidates();
	diagnostics_.models_evaluated = 0;
	
	const auto& data = ts.getValues();
	const int n = static_cast<int>(data.size());
	
	// Evaluate each candidate
	for (auto& config : candidates) {
		try {
			MFLES model(seasonal_periods_, config.iterations, 
			            config.lr_trend, config.lr_season, config.lr_level);
			model.fit(ts);
			
			// Compute AIC
			int k = 3;  // Base parameters
			for (int period : seasonal_periods_) {
				int K = std::min(period / 2, 10);
				k += 2 * K;
			}
			
			config.aic = computeAIC(model, n, k);
			diagnostics_.models_evaluated++;
			
		} catch (const std::exception&) {
			// Model failed to fit, use infinity AIC
			config.aic = std::numeric_limits<double>::infinity();
		}
	}
	
	// Find best configuration
	auto best_it = std::min_element(candidates.begin(), candidates.end());
	
	if (best_it == candidates.end() || 
	    best_it->aic == std::numeric_limits<double>::infinity()) {
		throw std::runtime_error("AutoMFLES: Failed to find any valid configuration");
	}
	
	// Store best parameters
	best_iterations_ = best_it->iterations;
	best_lr_trend_ = best_it->lr_trend;
	best_lr_season_ = best_it->lr_season;
	best_lr_level_ = best_it->lr_level;
	best_aic_ = best_it->aic;
	
	// Update diagnostics
	diagnostics_.best_aic = best_aic_;
	diagnostics_.best_iterations = best_iterations_;
	diagnostics_.best_lr_trend = best_lr_trend_;
	diagnostics_.best_lr_season = best_lr_season_;
	diagnostics_.best_lr_level = best_lr_level_;
}

} // namespace anofoxtime::models

