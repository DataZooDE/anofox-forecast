#include "anofox-time/models/auto_mstl.hpp"
#include "anofox-time/utils/logging.hpp"
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace anofoxtime::models {

AutoMSTL::AutoMSTL(
	std::vector<int> seasonal_periods,
	int mstl_iterations,
	bool robust
)
	: seasonal_periods_(std::move(seasonal_periods))
	, mstl_iterations_(std::max(1, mstl_iterations))
	, robust_(robust)
{
	if (seasonal_periods_.empty()) {
		throw std::invalid_argument("AutoMSTL: seasonal_periods cannot be empty");
	}
	
	for (int period : seasonal_periods_) {
		if (period < 2) {
			throw std::invalid_argument("AutoMSTL: all seasonal periods must be >= 2");
		}
	}
}

void AutoMSTL::fit(const core::TimeSeries& ts) {
	optimizeParameters(ts);
	is_fitted_ = true;
}

core::Forecast AutoMSTL::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("AutoMSTL: Must call fit() before predict()");
	}
	
	return best_model_->predict(horizon);
}

std::vector<AutoMSTL::Candidate> AutoMSTL::generateCandidates() {
	std::vector<Candidate> candidates;
	
	// All trend methods
	std::vector<MSTLForecaster::TrendMethod> trend_methods = {
		MSTLForecaster::TrendMethod::Linear,
		MSTLForecaster::TrendMethod::SES,
		MSTLForecaster::TrendMethod::Holt,
		MSTLForecaster::TrendMethod::None,
		MSTLForecaster::TrendMethod::AutoETSTrendAdditive,
		MSTLForecaster::TrendMethod::AutoETSTrendMultiplicative
	};
	
	// All seasonal methods
	std::vector<MSTLForecaster::SeasonalMethod> seasonal_methods = {
		MSTLForecaster::SeasonalMethod::Cyclic,
		MSTLForecaster::SeasonalMethod::AutoETSAdditive,
		MSTLForecaster::SeasonalMethod::AutoETSMultiplicative
	};
	
	// Generate all combinations (6 × 3 = 18 candidates)
	for (auto trend : trend_methods) {
		for (auto seasonal : seasonal_methods) {
			candidates.push_back({trend, seasonal});
		}
	}
	
	return candidates;
}

double AutoMSTL::computeAIC(const MSTLForecaster& model, int n) {
	// Get fitted values by predicting in-sample
	// We'll compute residuals by comparing original data to the decomposition
	const auto& components = model.components();
	const auto& trend = components.trend;
	const auto& remainder = components.remainder;
	
	// Compute residual sum of squares
	double sse = 0.0;
	for (size_t i = 0; i < remainder.size(); ++i) {
		sse += remainder[i] * remainder[i];
	}
	
	// Compute log-likelihood (assuming Gaussian errors)
	double sigma2 = sse / n;
	if (sigma2 <= 0.0) {
		sigma2 = 1e-10; // Avoid log(0)
	}
	
	double log_likelihood = -0.5 * n * (std::log(2.0 * M_PI * sigma2) + 1.0);
	
	// Count parameters
	// For MSTL, we have:
	// - Trend parameters (depends on trend method)
	// - Seasonal parameters (depends on seasonal method)
	// - Noise variance (1 parameter)
	int k = 1; // Start with noise variance
	
	// Trend parameters estimation
	switch (model.trendMethod()) {
		case MSTLForecaster::TrendMethod::Linear:
			k += 2; // slope + intercept
			break;
		case MSTLForecaster::TrendMethod::SES:
			k += 2; // alpha + level
			break;
		case MSTLForecaster::TrendMethod::Holt:
			k += 4; // alpha + beta + level + trend
			break;
		case MSTLForecaster::TrendMethod::None:
			k += 0; // No parameters
			break;
		case MSTLForecaster::TrendMethod::AutoETSTrendAdditive:
		case MSTLForecaster::TrendMethod::AutoETSTrendMultiplicative:
			k += 4; // ETS parameters (error, level, trend, smoothing params)
			break;
	}
	
	// Seasonal parameters estimation (per seasonal component)
	int num_seasonal = static_cast<int>(seasonal_periods_.size());
	switch (model.seasonalMethod()) {
		case MSTLForecaster::SeasonalMethod::Cyclic:
			// Cyclic uses last period values, minimal params
			for (int period : seasonal_periods_) {
				k += period; // Store one cycle per period
			}
			break;
		case MSTLForecaster::SeasonalMethod::AutoETSAdditive:
		case MSTLForecaster::SeasonalMethod::AutoETSMultiplicative:
			// ETS seasonal models
			for (int period : seasonal_periods_) {
				k += period + 2; // Initial seasonal indices + smoothing params
			}
			break;
	}
	
	// AIC = -2 * log_likelihood + 2 * k
	double aic = -2.0 * log_likelihood + 2.0 * k;
	
	return aic;
}

void AutoMSTL::optimizeParameters(const core::TimeSeries& ts) {
	auto start_time = std::chrono::high_resolution_clock::now();
	
	auto candidates = generateCandidates();
	diagnostics_.models_evaluated = 0;
	
	ANOFOX_INFO("AutoMSTL: Testing {} candidate configurations", candidates.size());
	
	for (const auto& candidate : candidates) {
		try {
			// Create MSTL model with this configuration
			MSTLForecaster model(
				seasonal_periods_,
				candidate.trend,
				candidate.seasonal,
				mstl_iterations_,
				robust_
			);
			
			// Fit the model
			model.fit(ts);
			
			// Compute AIC
			double aic = computeAIC(model, static_cast<int>(ts.getValues().size()));
			
			diagnostics_.models_evaluated++;
			
			// Update best model if this is better
			if (aic < best_aic_) {
				best_aic_ = aic;
				best_model_ = std::make_unique<MSTLForecaster>(
					seasonal_periods_,
					candidate.trend,
					candidate.seasonal,
					mstl_iterations_,
					robust_
				);
				best_model_->fit(ts);
				
				diagnostics_.best_aic = best_aic_;
				diagnostics_.best_trend = candidate.trend;
				diagnostics_.best_seasonal = candidate.seasonal;
			}
			
		} catch (const std::exception& e) {
			// Skip this configuration if it fails to fit
			ANOFOX_DEBUG("AutoMSTL: Skipped candidate (trend={}, seasonal={}) due to: {}",
				static_cast<int>(candidate.trend),
				static_cast<int>(candidate.seasonal),
				e.what());
			continue;
		}
	}
	
	auto end_time = std::chrono::high_resolution_clock::now();
	diagnostics_.optimization_time_ms = 
		std::chrono::duration<double, std::milli>(end_time - start_time).count();
	
	if (!best_model_) {
		throw std::runtime_error("AutoMSTL: Failed to fit any valid model");
	}
	
	ANOFOX_INFO("AutoMSTL: Selected trend={}, seasonal={}, AIC={:.2f}, evaluated {} models in {:.2f}ms",
		static_cast<int>(diagnostics_.best_trend),
		static_cast<int>(diagnostics_.best_seasonal),
		diagnostics_.best_aic,
		diagnostics_.models_evaluated,
		diagnostics_.optimization_time_ms);
}

} // namespace anofoxtime::models

