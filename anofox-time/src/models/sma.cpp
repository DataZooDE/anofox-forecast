#include "anofox-time/models/sma.hpp"
#include <numeric>
#include <stdexcept>

namespace anofoxtime::models {

// --- Model Implementation ---

SimpleMovingAverage::SimpleMovingAverage(int window) : window_(window) {
	if (window_ < 0) {
		throw std::invalid_argument("Window size must be non-negative.");
	}
	// NOTE: window_ == 0 means use full history for averaging
	// NOTE: Logging moved to builder to avoid duplication.
}

void SimpleMovingAverage::fit(const core::TimeSeries &ts) {
	if (ts.dimensions() != 1) {
		throw std::invalid_argument("SimpleMovingAverage currently supports univariate series only.");
	}
	// When window_ is 0, we use full history, so no minimum size check
	if (window_ > 0 && ts.size() < static_cast<size_t>(window_)) {
		throw std::invalid_argument("Time series size must be at least equal to the window size.");
	}
	if (ts.size() == 0) {
		throw std::invalid_argument("Cannot fit on empty time series.");
	}
	history_ = ts.getValues();
	is_fitted_ = true;
	if (window_ == 0) {
		ANOFOX_INFO("SMA model fitted with {} data points (using full history).", ts.size());
	} else {
		ANOFOX_INFO("SMA model fitted with {} data points (window={}).", ts.size(), window_);
	}
}

core::Forecast SimpleMovingAverage::predict(int horizon) {
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

	// Calculate the average of the last window from historical data ONCE
	// Then repeat this constant forecast for all horizons (matches statsforecast)
	int effective_window = (window_ == 0) ? static_cast<int>(history_.size()) : window_;
	effective_window = std::min(effective_window, static_cast<int>(history_.size()));
	
	double sum = std::accumulate(history_.end() - effective_window, history_.end(), 0.0);
	double constant_forecast = sum / static_cast<double>(effective_window);

	// Repeat the constant forecast for all horizons
	for (int i = 0; i < horizon; ++i) {
		series.push_back(constant_forecast);
	}

	return forecast;
}

// --- Builder Implementation ---

SimpleMovingAverageBuilder &SimpleMovingAverageBuilder::withWindow(int window) {
	window_ = window;
	return *this;
}

std::unique_ptr<SimpleMovingAverage> SimpleMovingAverageBuilder::build() {
	ANOFOX_DEBUG("Building SMA model with window size {}.", window_);
	return std::unique_ptr<SimpleMovingAverage>(new SimpleMovingAverage(window_));
}

} // namespace anofoxtime::models
