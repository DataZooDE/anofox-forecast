#include "anofox-time/models/ses_optimized.hpp"
#include "anofox-time/utils/logging.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace anofoxtime::models {

namespace {
	constexpr double kMinAlpha = 0.05;
	constexpr double kMaxAlpha = 0.95;
	constexpr double kAlphaStep = 0.05;
}

SESOptimized::SESOptimized() 
    : optimal_alpha_(0.5), optimal_mse_(std::numeric_limits<double>::infinity()) {}

double SESOptimized::optimizeAlpha(const std::vector<double>& data) {
	if (data.size() < 2) {
		return 0.5;  // Default for insufficient data
	}
	
	double best_alpha = 0.5;
	double best_mse = std::numeric_limits<double>::infinity();
	
	ANOFOX_DEBUG("SESOptimized: Starting alpha optimization...");
	
	// Grid search for optimal alpha
	for (double alpha = kMinAlpha; alpha <= kMaxAlpha; alpha += kAlphaStep) {
		double level = data[0];
		double mse = 0.0;
		
		// Compute one-step-ahead forecast errors
		for (std::size_t i = 1; i < data.size(); ++i) {
			const double forecast = level;
			const double error = data[i] - forecast;
			mse += error * error;
			
			// Update level
			level = alpha * data[i] + (1.0 - alpha) * level;
		}
		
		mse /= static_cast<double>(data.size() - 1);
		
		if (mse < best_mse) {
			best_mse = mse;
			best_alpha = alpha;
		}
	}
	
	optimal_mse_ = best_mse;
	
	ANOFOX_INFO("SESOptimized: Optimal alpha={:.3f}, MSE={:.4f}", best_alpha, best_mse);
	
	return best_alpha;
}

void SESOptimized::fit(const core::TimeSeries& ts) {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("SESOptimized currently supports univariate series only");
	}
	
	const auto data = ts.getValues();
	
	if (data.empty()) {
		throw std::invalid_argument("Cannot fit SESOptimized on empty time series");
	}
	
	// Optimize alpha
	optimal_alpha_ = optimizeAlpha(data);
	
	// Fit final model with optimal alpha using builder
	fitted_model_ = SimpleExponentialSmoothingBuilder()
		.withAlpha(optimal_alpha_)
		.build();
	fitted_model_->fit(ts);
	
	is_fitted_ = true;
	
	ANOFOX_INFO("SESOptimized model fitted with alpha={:.3f}", optimal_alpha_);
}

core::Forecast SESOptimized::predict(int horizon) {
	if (!is_fitted_ || !fitted_model_) {
		throw std::runtime_error("SESOptimized::predict called before fit");
	}
	
	return fitted_model_->predict(horizon);
}

core::Forecast SESOptimized::predictWithConfidence(int horizon, double confidence) {
	if (!is_fitted_ || !fitted_model_) {
		throw std::runtime_error("SESOptimized::predictWithConfidence called before fit");
	}
	
	// SES doesn't have built-in confidence intervals, compute from residuals
	auto forecast = predict(horizon);
	
	// For now, return forecast without intervals (can be enhanced later)
	return forecast;
}

const std::vector<double>& SESOptimized::fittedValues() const {
	static const std::vector<double> empty;
	if (!fitted_model_) {
		return empty;
	}
	// SES doesn't expose fitted values yet, return empty for now
	return empty;
}

const std::vector<double>& SESOptimized::residuals() const {
	static const std::vector<double> empty;
	if (!fitted_model_) {
		return empty;
	}
	// SES doesn't expose residuals yet, return empty for now
	return empty;
}

} // namespace anofoxtime::models

