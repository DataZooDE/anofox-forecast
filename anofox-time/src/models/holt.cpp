#include "anofox-time/models/holt.hpp"
#include <stdexcept>

namespace anofoxtime::models {

// --- Model Implementation ---

HoltLinearTrend::HoltLinearTrend(double alpha, double beta) : alpha_(alpha), beta_(beta) {
	if (alpha_ < 0.0 || alpha_ > 1.0) {
		throw std::invalid_argument("Alpha must be between 0 and 1.");
	}
	if (beta_ < 0.0 || beta_ > 1.0) {
		throw std::invalid_argument("Beta must be between 0 and 1.");
	}
}

void HoltLinearTrend::fit(const core::TimeSeries &ts) {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("HoltLinearTrend currently supports univariate series only.");
	}
	const auto &values = ts.getValues();
	if (values.size() < 2) {
		throw std::invalid_argument("Time series must have at least 2 data points for Holt's method.");
	}

	// Initialize level and trend
	double current_level = values[0];
	double current_trend = values[1] - values[0];

	for (size_t i = 1; i < values.size(); ++i) {
		double last_level = current_level;
		current_level = alpha_ * values[i] + (1.0 - alpha_) * (last_level + current_trend);
		current_trend = beta_ * (current_level - last_level) + (1.0 - beta_) * current_trend;
	}

	last_level_ = current_level;
	last_trend_ = current_trend;
	is_fitted_ = true;
	ANOFOX_INFO("Holt model fitted with {} data points. Final level = {}, Final trend = {}.", values.size(),
	            last_level_, last_trend_);
}

core::Forecast HoltLinearTrend::predict(int horizon) {
	if (!is_fitted_) {
		throw std::runtime_error("Predict called before fit.");
	}
	if (horizon < 0) {
		throw std::invalid_argument("Forecast horizon must be non-negative.");
	}
	if (horizon == 0) {
		return {};
	}

	ANOFOX_INFO("Predicting {} steps ahead.", horizon);

	core::Forecast forecast;
	auto &series = forecast.primary();
	series.reserve(horizon);

	for (int h = 1; h <= horizon; ++h) {
		series.push_back(last_level_ + h * last_trend_);
	}

	return forecast;
}

// --- Builder Implementation ---

HoltLinearTrendBuilder &HoltLinearTrendBuilder::withAlpha(double alpha) {
	alpha_ = alpha;
	return *this;
}

HoltLinearTrendBuilder &HoltLinearTrendBuilder::withBeta(double beta) {
	beta_ = beta;
	return *this;
}

std::unique_ptr<HoltLinearTrend> HoltLinearTrendBuilder::build() {
	ANOFOX_DEBUG("Building Holt model with alpha = {} and beta = {}.", alpha_, beta_);
	return std::unique_ptr<HoltLinearTrend>(new HoltLinearTrend(alpha_, beta_));
}

} // namespace anofoxtime::models
