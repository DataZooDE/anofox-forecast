#include "anofox-time/models/ses.hpp"
#include <stdexcept>

namespace anofoxtime::models {

// --- Model Implementation ---

SimpleExponentialSmoothing::SimpleExponentialSmoothing(double alpha) : alpha_(alpha) {
	if (alpha_ < 0.0 || alpha_ > 1.0) {
		throw std::invalid_argument("Alpha must be between 0 and 1.");
	}
}

void SimpleExponentialSmoothing::fit(const core::TimeSeries &ts) {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("SimpleExponentialSmoothing currently supports univariate series only.");
	}
	const auto &values = ts.getValues();
	if (values.empty()) {
		throw std::invalid_argument("Time series cannot be empty for fitting.");
	}

	// Initialize the first level with the first observation
	last_level_ = values[0];

	// Iterate through the rest of the series to calculate the final level
	for (size_t i = 1; i < values.size(); ++i) {
		last_level_ = alpha_ * values[i] + (1.0 - alpha_) * last_level_;
	}

	is_fitted_ = true;
	ANOFOX_INFO("SES model fitted with {} data points. Final level = {}.", values.size(), last_level_);
}

core::Forecast SimpleExponentialSmoothing::predict(int horizon) {
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

	// For SES, the forecast for all future points is simply the last calculated level.
	core::Forecast forecast;
	auto &series = forecast.primary();
	series.assign(horizon, last_level_);

	return forecast;
}

// --- Builder Implementation ---

SimpleExponentialSmoothingBuilder &SimpleExponentialSmoothingBuilder::withAlpha(double alpha) {
	alpha_ = alpha;
	return *this;
}

std::unique_ptr<SimpleExponentialSmoothing> SimpleExponentialSmoothingBuilder::build() {
	ANOFOX_DEBUG("Building SES model with alpha = {}.", alpha_);
	return std::unique_ptr<SimpleExponentialSmoothing>(new SimpleExponentialSmoothing(alpha_));
}

} // namespace anofoxtime::models
