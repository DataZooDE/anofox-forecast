#include "anofox-time/models/seasonal_es_optimized.hpp"
#include "anofox-time/utils/logging.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>

namespace anofoxtime::models {

namespace {
	constexpr double kMinParam = 0.05;
	constexpr double kMaxParam = 0.95;
	constexpr double kParamStep = 0.10;
}

SeasonalESOptimized::SeasonalESOptimized(int seasonal_period)
    : seasonal_period_(seasonal_period), optimal_alpha_(0.2), optimal_gamma_(0.1),
      optimal_mse_(std::numeric_limits<double>::infinity()) {
	if (seasonal_period_ < 2) {
		throw std::invalid_argument("Seasonal period must be >= 2");
	}
}

SeasonalESOptimized::OptResult SeasonalESOptimized::optimize(const std::vector<double>& data) {
	double best_mse = std::numeric_limits<double>::infinity();
	OptResult best_result = {0.2, 0.1, best_mse};
	
	int evaluations = 0;
	
	ANOFOX_INFO("SeasonalESOptimized: Starting parameter optimization...");
	
	// Grid search over alpha and gamma
	for (double alpha = kMinParam; alpha <= kMaxParam; alpha += kParamStep) {
		for (double gamma = kMinParam; gamma <= kMaxParam; gamma += kParamStep) {
			try {
				SeasonalExponentialSmoothing model(seasonal_period_, alpha, gamma);
				
				// Fit on data using TimeSeries wrapper
				std::vector<core::TimeSeries::TimePoint> timestamps(data.size());
				auto start = core::TimeSeries::TimePoint{};
				for (std::size_t i = 0; i < data.size(); ++i) {
					timestamps[i] = start + std::chrono::seconds(static_cast<long>(i));
				}
				core::TimeSeries ts(std::move(timestamps), data);
				
				model.fit(ts);
				
				// Compute MSE from residuals
				const auto& residuals = model.residuals();
				if (residuals.size() > static_cast<std::size_t>(seasonal_period_)) {
					double mse = 0.0;
					for (std::size_t i = seasonal_period_; i < residuals.size(); ++i) {
						mse += residuals[i] * residuals[i];
					}
					mse /= static_cast<double>(residuals.size() - seasonal_period_);
					
					evaluations++;
					
					if (mse < best_mse) {
						best_mse = mse;
						best_result = {alpha, gamma, mse};
						ANOFOX_DEBUG("SeasonalESOptimized: New best - alpha={:.2f}, gamma={:.2f}, MSE={:.2f}",
						             alpha, gamma, mse);
					}
				}
			} catch (const std::exception& e) {
				// Skip this combination
			}
		}
	}
	
	ANOFOX_INFO("SeasonalESOptimized: Evaluated {} parameter combinations", evaluations);
	ANOFOX_INFO("SeasonalESOptimized: Optimal - alpha={:.2f}, gamma={:.2f}, MSE={:.2f}",
	            best_result.alpha, best_result.gamma, best_result.mse);
	
	return best_result;
}

void SeasonalESOptimized::fit(const core::TimeSeries& ts) {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("SeasonalESOptimized currently supports univariate series only");
	}
	
	const auto data = ts.getValues();
	
	if (data.empty()) {
		throw std::invalid_argument("Cannot fit on empty time series");
	}
	
	// Optimize parameters
	auto result = optimize(data);
	
	optimal_alpha_ = result.alpha;
	optimal_gamma_ = result.gamma;
	optimal_mse_ = result.mse;
	
	// Fit final model with optimal parameters
	fitted_model_ = std::make_unique<SeasonalExponentialSmoothing>(
		seasonal_period_, optimal_alpha_, optimal_gamma_);
	fitted_model_->fit(ts);
	
	is_fitted_ = true;
	
	ANOFOX_INFO("SeasonalESOptimized model fitted with alpha={:.2f}, gamma={:.2f}",
	            optimal_alpha_, optimal_gamma_);
}

core::Forecast SeasonalESOptimized::predict(int horizon) {
	if (!is_fitted_ || !fitted_model_) {
		throw std::runtime_error("SeasonalESOptimized::predict called before fit");
	}
	
	return fitted_model_->predict(horizon);
}

core::Forecast SeasonalESOptimized::predictWithConfidence(int horizon, double confidence) {
	if (!is_fitted_ || !fitted_model_) {
		throw std::runtime_error("SeasonalESOptimized::predictWithConfidence called before fit");
	}
	
	return fitted_model_->predictWithConfidence(horizon, confidence);
}

const std::vector<double>& SeasonalESOptimized::fittedValues() const {
	if (!fitted_model_) {
		static const std::vector<double> empty;
		return empty;
	}
	return fitted_model_->fittedValues();
}

const std::vector<double>& SeasonalESOptimized::residuals() const {
	if (!fitted_model_) {
		static const std::vector<double> empty;
		return empty;
	}
	return fitted_model_->residuals();
}

} // namespace anofoxtime::models

