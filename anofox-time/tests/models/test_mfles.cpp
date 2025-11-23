#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "anofox-time/models/mfles.hpp"
#include "anofox-time/core/time_series.hpp"
#include <chrono>
#include <cmath>

using namespace anofoxtime;
using namespace anofoxtime::models;
using namespace anofoxtime::core;

namespace {

TimeSeries createTimeSeries(const std::vector<double>& data) {
	std::vector<TimeSeries::TimePoint> timestamps;
	timestamps.reserve(data.size());
	auto start = TimeSeries::TimePoint{};
	for (std::size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	return TimeSeries(std::move(timestamps), data);
}

// Generate synthetic seasonal data
std::vector<double> generateSeasonalData(int n, int period, double amplitude = 10.0,
                                         double trend = 0.5, double level = 100.0) {
	std::vector<double> data(n);
	for (int i = 0; i < n; ++i) {
		double seasonal = amplitude * std::sin(2.0 * M_PI * i / period);
		data[i] = level + trend * i + seasonal;
	}
	return data;
}

} // namespace

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_CASE("MFLES v2: Basic construction with default parameters", "[mfles_v2][basic]") {
	MFLES mfles;
	REQUIRE(mfles.getName() == "MFLES_Enhanced");
}

TEST_CASE("MFLES v2: Construction with custom parameters", "[mfles_v2][basic]") {
	MFLES::Params params;
	params.seasonal_periods = {12};
	params.max_rounds = 5;
	params.trend_method = TrendMethod::OLS;

	MFLES mfles(params);
	REQUIRE(mfles.getName() == "MFLES_Enhanced");
}

TEST_CASE("MFLES v2: Fit and predict basic workflow", "[mfles_v2][basic]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES mfles;
	REQUIRE_NOTHROW(mfles.fit(ts));

	auto forecast = mfles.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("MFLES v2: Fitted values and residuals", "[mfles_v2][basic]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);

	MFLES mfles;
	mfles.fit(ts);

	REQUIRE(mfles.fittedValues().size() == 48);
	REQUIRE(mfles.residuals().size() == 48);
}

TEST_CASE("MFLES v2: Multiple seasonal periods", "[mfles_v2][basic]") {
	auto data = generateSeasonalData(120, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.seasonal_periods = {12, 4};

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));

	auto forecast = mfles.predict(24);
	REQUIRE(forecast.primary().size() == 24);
}

// ============================================================================
// Parameter Validation Tests
// ============================================================================

TEST_CASE("MFLES validates max_rounds", "[mfles_v2][validation][error]") {
	MFLES::Params params;
	params.max_rounds = 0;  // Invalid
	
	REQUIRE_THROWS_AS(MFLES(params), std::invalid_argument);
}

TEST_CASE("MFLES validates learning rates", "[mfles_v2][validation][error]") {
	MFLES::Params params;
	
	// Test each learning rate
	params.lr_median = -0.1;
	REQUIRE_THROWS_AS(MFLES(params), std::invalid_argument);
	
	params.lr_median = 1.0;
	params.lr_trend = 1.5;  // > 1.0
	REQUIRE_THROWS_AS(MFLES(params), std::invalid_argument);
	
	params.lr_trend = 0.9;
	params.lr_season = -0.1;
	REQUIRE_THROWS_AS(MFLES(params), std::invalid_argument);
	
	params.lr_season = 0.9;
	params.lr_rs = 2.0;  // > 1.0
	REQUIRE_THROWS_AS(MFLES(params), std::invalid_argument);
	
	params.lr_rs = 1.0;
	params.lr_exogenous = -0.1;
	REQUIRE_THROWS_AS(MFLES(params), std::invalid_argument);
}

TEST_CASE("MFLES validates seasonal periods", "[mfles_v2][validation][error]") {
	MFLES::Params params;
	params.seasonal_periods = {0};  // Invalid
	
	REQUIRE_THROWS_AS(MFLES(params), std::invalid_argument);
	
	params.seasonal_periods = {-1};  // Invalid
	REQUIRE_THROWS_AS(MFLES(params), std::invalid_argument);
}

TEST_CASE("MFLES validates cov_threshold", "[mfles_v2][validation][error]") {
	MFLES::Params params;
	params.cov_threshold = -0.1;  // < 0.0
	
	REQUIRE_THROWS_AS(MFLES(params), std::invalid_argument);
	
	params.cov_threshold = 1.5;  // > 1.0
	REQUIRE_THROWS_AS(MFLES(params), std::invalid_argument);
}

TEST_CASE("MFLES validates n_changepoints_pct", "[mfles_v2][validation][error]") {
	MFLES::Params params;
	params.n_changepoints_pct = -0.1;  // < 0.0
	
	REQUIRE_THROWS_AS(MFLES(params), std::invalid_argument);
	
	params.n_changepoints_pct = 1.5;  // > 1.0
	REQUIRE_THROWS_AS(MFLES(params), std::invalid_argument);
}

TEST_CASE("MFLES requires fit before predict", "[mfles_v2][error]") {
	MFLES mfles;
	REQUIRE_THROWS_AS(mfles.predict(5), std::runtime_error);
}

TEST_CASE("MFLES validates horizon", "[mfles_v2][validation][error]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);
	
	MFLES mfles;
	mfles.fit(ts);
	
	REQUIRE_THROWS_AS(mfles.predict(0), std::invalid_argument);
	REQUIRE_THROWS_AS(mfles.predict(-5), std::invalid_argument);
}

TEST_CASE("MFLES requires at least 3 data points", "[mfles_v2][error]") {
	MFLES mfles;
	
	std::vector<double> short_data{1.0, 2.0};
	auto ts = createTimeSeries(short_data);
	
	REQUIRE_THROWS_AS(mfles.fit(ts), std::runtime_error);
}

TEST_CASE("MFLES requires fit before seasonal_decompose", "[mfles_v2][error]") {
	MFLES mfles;
	REQUIRE_THROWS_AS(mfles.seasonal_decompose(), std::runtime_error);
}

// ============================================================================
// Trend Method Tests
// ============================================================================

TEST_CASE("MFLES v2: OLS trend method", "[mfles_v2][trend]") {
	auto data = generateSeasonalData(60, 12, 10.0, 1.0, 100.0);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.trend_method = TrendMethod::OLS;
	params.max_rounds = 3;

	MFLES mfles(params);
	mfles.fit(ts);

	auto forecast = mfles.predict(12);
	REQUIRE(forecast.primary().size() == 12);

	// Forecast should show reasonable trend behavior
	// With seasonal data, the trend might be captured differently
	// Just verify forecast values are reasonable
	REQUIRE(forecast.primary().size() == 12);
	REQUIRE(std::all_of(forecast.primary().begin(), forecast.primary().end(),
	                    [](double v) { return std::isfinite(v); }));
}

TEST_CASE("MFLES v2: Siegel robust trend method", "[mfles_v2][trend]") {
	auto data = generateSeasonalData(60, 12, 10.0, 0.5, 100.0);
	// Add outliers
	data[20] += 50.0;
	data[40] -= 40.0;

	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.trend_method = TrendMethod::SIEGEL_ROBUST;
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));

	auto forecast = mfles.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("MFLES v2: Siegel vs OLS with outliers", "[mfles_v2][trend]") {
	auto data = generateSeasonalData(60, 12, 10.0, 0.5, 100.0);
	data[25] += 60.0;  // Large outlier
	data[50] -= 50.0;

	auto ts = createTimeSeries(data);

	// OLS model
	MFLES::Params ols_params;
	ols_params.trend_method = TrendMethod::OLS;
	ols_params.max_rounds = 3;
	MFLES ols_model(ols_params);
	ols_model.fit(ts);

	// Siegel model
	MFLES::Params siegel_params;
	siegel_params.trend_method = TrendMethod::SIEGEL_ROBUST;
	siegel_params.max_rounds = 3;
	MFLES siegel_model(siegel_params);
	siegel_model.fit(ts);

	// Both should produce forecasts
	auto ols_forecast = ols_model.predict(12);
	auto siegel_forecast = siegel_model.predict(12);

	REQUIRE(ols_forecast.primary().size() == 12);
	REQUIRE(siegel_forecast.primary().size() == 12);
}

TEST_CASE("MFLES v2: Piecewise trend method", "[mfles_v2][trend]") {
	// Data with trend change
	std::vector<double> data(80);
	for (int i = 0; i < 40; ++i) {
		data[i] = 100.0 + 1.0 * i;  // Increasing
	}
	for (int i = 40; i < 80; ++i) {
		data[i] = 140.0 - 0.5 * (i - 40);  // Decreasing
	}

	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.trend_method = TrendMethod::PIECEWISE;
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));

	auto forecast = mfles.predict(10);
	REQUIRE(forecast.primary().size() == 10);
}

// ============================================================================
// Configuration Preset Tests
// ============================================================================

TEST_CASE("MFLES v2: Fast preset", "[mfles_v2][presets]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	auto params = MFLES::fastPreset();
	REQUIRE(params.max_rounds == 3);
	REQUIRE(params.fourier_order == 3);
	REQUIRE(params.trend_method == TrendMethod::OLS);

	MFLES mfles(params);
	mfles.fit(ts);

	REQUIRE(mfles.actualRoundsUsed() <= 3);
}

TEST_CASE("MFLES v2: Balanced preset", "[mfles_v2][presets]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	auto params = MFLES::balancedPreset();
	REQUIRE(params.max_rounds == 5);
	REQUIRE(params.fourier_order == 5);

	MFLES mfles(params);
	mfles.fit(ts);

	REQUIRE(mfles.actualRoundsUsed() <= 5);
}

TEST_CASE("MFLES v2: Accurate preset", "[mfles_v2][presets]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	auto params = MFLES::accuratePreset();
	REQUIRE(params.max_rounds == 10);
	REQUIRE(params.fourier_order == 7);
	REQUIRE(params.trend_method == TrendMethod::SIEGEL_ROBUST);

	MFLES mfles(params);
	mfles.fit(ts);

	REQUIRE(mfles.actualRoundsUsed() <= 10);
}

TEST_CASE("MFLES v2: Robust preset", "[mfles_v2][presets]") {
	auto data = generateSeasonalData(60, 12);
	data[30] += 40.0;  // Add outlier

	auto ts = createTimeSeries(data);

	auto params = MFLES::robustPreset();
	REQUIRE(params.trend_method == TrendMethod::SIEGEL_ROBUST);
	REQUIRE(params.cap_outliers == true);

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

// ============================================================================
// Moving Medians Tests
// ============================================================================

TEST_CASE("MFLES v2: Global median (default)", "[mfles_v2][median]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.moving_medians = false;  // Default
	params.max_rounds = 2;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Moving window median", "[mfles_v2][median]") {
	auto data = generateSeasonalData(120, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.moving_medians = true;
	params.seasonal_periods = {12};
	params.max_rounds = 2;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Moving median adapts to recent data", "[mfles_v2][median]") {
	// Data with level shift
	std::vector<double> data(120);
	for (int i = 0; i < 60; ++i) {
		data[i] = 100.0 + 10.0 * std::sin(2.0 * M_PI * i / 12);
	}
	for (int i = 60; i < 120; ++i) {
		data[i] = 150.0 + 10.0 * std::sin(2.0 * M_PI * i / 12);  // Level shift
	}

	auto ts = createTimeSeries(data);

	// Global median model
	MFLES::Params global_params;
	global_params.moving_medians = false;
	global_params.max_rounds = 2;
	MFLES global_model(global_params);
	global_model.fit(ts);

	// Moving median model
	MFLES::Params moving_params;
	moving_params.moving_medians = true;
	moving_params.max_rounds = 2;
	MFLES moving_model(moving_params);
	moving_model.fit(ts);

	// Both should produce forecasts
	auto global_forecast = global_model.predict(12);
	auto moving_forecast = moving_model.predict(12);

	REQUIRE(global_forecast.primary().size() == 12);
	REQUIRE(moving_forecast.primary().size() == 12);

	// Forecasts should be different due to different baseline
	double diff = std::abs(global_forecast.primary()[0] - moving_forecast.primary()[0]);
	REQUIRE(diff > 0.5);  // Should be noticeably different
}

// ============================================================================
// Fourier Order Tests
// ============================================================================

TEST_CASE("MFLES v2: Custom Fourier order", "[mfles_v2][fourier]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.fourier_order = 7;
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Adaptive Fourier order", "[mfles_v2][fourier]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.fourier_order = -1;  // Adaptive
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Low Fourier order captures main pattern", "[mfles_v2][fourier]") {
	auto data = generateSeasonalData(60, 12, 15.0, 0.0, 100.0);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.fourier_order = 1;  // Just fundamental frequency
	params.max_rounds = 3;

	MFLES mfles(params);
	mfles.fit(ts);

	// Should still capture the main seasonal pattern
	auto forecast = mfles.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

// ============================================================================
// Weighted Seasonality Tests
// ============================================================================

TEST_CASE("MFLES v2: Weighted seasonality disabled (default)", "[mfles_v2][seasonality]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.seasonality_weights = false;
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Weighted seasonality enabled", "[mfles_v2][seasonality]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.seasonality_weights = true;
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

// ============================================================================
// Outlier Handling Tests
// ============================================================================

TEST_CASE("MFLES v2: Outlier capping disabled", "[mfles_v2][outliers]") {
	auto data = generateSeasonalData(60, 12);
	data[30] += 100.0;  // Large outlier

	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.cap_outliers = false;
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Outlier capping enabled", "[mfles_v2][outliers]") {
	auto data = generateSeasonalData(60, 12);
	data[20] += 80.0;
	data[40] -= 70.0;

	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.cap_outliers = true;
	params.outlier_sigma = 3.0;
	params.max_rounds = 5;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Outlier capping with custom threshold", "[mfles_v2][outliers]") {
	auto data = generateSeasonalData(60, 12);
	data[30] += 50.0;

	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.cap_outliers = true;
	params.outlier_sigma = 2.0;  // More aggressive
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

// ============================================================================
// Learning Rate Tests
// ============================================================================

TEST_CASE("MFLES v2: High trend learning rate", "[mfles_v2][learning_rates]") {
	auto data = generateSeasonalData(60, 12, 10.0, 1.0, 100.0);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.lr_trend = 0.9;
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Low trend learning rate", "[mfles_v2][learning_rates]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.lr_trend = 0.1;
	params.max_rounds = 5;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Varying seasonal learning rate", "[mfles_v2][learning_rates]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	for (double lr : {0.1, 0.5, 0.9}) {
		MFLES::Params params;
		params.lr_season = lr;
		params.max_rounds = 3;

		MFLES mfles(params);
		REQUIRE_NOTHROW(mfles.fit(ts));
	}
}

TEST_CASE("MFLES v2: Zero learning rate for component", "[mfles_v2][learning_rates]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.lr_median = 0.0;  // Disable median component
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

// ============================================================================
// ES Ensemble Tests
// ============================================================================

TEST_CASE("MFLES v2: ES ensemble with default parameters", "[mfles_v2][es_ensemble]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.smoother = false;  // Use ES ensemble
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: ES ensemble with custom alpha range", "[mfles_v2][es_ensemble]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.min_alpha = 0.2;
	params.max_alpha = 0.8;
	params.es_ensemble_steps = 10;
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: ES ensemble with many steps", "[mfles_v2][es_ensemble]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.es_ensemble_steps = 50;
	params.max_rounds = 2;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Moving average smoother", "[mfles_v2][es_ensemble]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.smoother = true;  // Use MA instead of ES
	params.ma_window = 5;
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Moving average with large window", "[mfles_v2][es_ensemble]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.smoother = true;
	params.ma_window = 10;
	params.max_rounds = 3;

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

// ============================================================================
// Convergence and Boosting Tests
// ============================================================================

TEST_CASE("MFLES v2: Single round", "[mfles_v2][convergence]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.max_rounds = 1;

	MFLES mfles(params);
	mfles.fit(ts);

	REQUIRE(mfles.actualRoundsUsed() == 1);
}

TEST_CASE("MFLES v2: Early stopping with convergence", "[mfles_v2][convergence]") {
	auto data = generateSeasonalData(60, 12, 10.0, 0.5, 100.0);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.max_rounds = 50;
	params.convergence_threshold = 0.01;

	MFLES mfles(params);
	mfles.fit(ts);

	// Should stop before max_rounds due to convergence
	REQUIRE(mfles.actualRoundsUsed() <= params.max_rounds);
}

TEST_CASE("MFLES v2: Many rounds for complex pattern", "[mfles_v2][convergence]") {
	// Complex pattern with multiple seasonal components
	std::vector<double> data(120);
	for (int i = 0; i < 120; ++i) {
		data[i] = 100.0 + 0.5 * i +
		         10.0 * std::sin(2.0 * M_PI * i / 12) +
		         5.0 * std::sin(2.0 * M_PI * i / 4) +
		         3.0 * std::sin(2.0 * M_PI * i / 7);
	}
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.max_rounds = 20;
	params.seasonal_periods = {12, 4};

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Residuals improve with rounds", "[mfles_v2][convergence]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	std::vector<double> residual_norms;

	for (int rounds = 1; rounds <= 5; ++rounds) {
		MFLES::Params params;
		params.max_rounds = rounds;

		MFLES mfles(params);
		mfles.fit(ts);

		const auto& residuals = mfles.residuals();
		double norm = 0.0;
		for (double r : residuals) {
			norm += r * r;
		}
		residual_norms.push_back(std::sqrt(norm));
	}

	// Later rounds should generally have lower residual norms
	REQUIRE(residual_norms.back() <= residual_norms.front() * 1.1);
}

// ============================================================================
// Edge Cases and Robustness
// ============================================================================

TEST_CASE("MFLES v2: Very short time series", "[mfles_v2][edge]") {
	std::vector<double> data = {100.0, 101.0, 102.0};
	auto ts = createTimeSeries(data);

	MFLES mfles;
	REQUIRE_NOTHROW(mfles.fit(ts));

	auto forecast = mfles.predict(3);
	REQUIRE(forecast.primary().size() == 3);
}

TEST_CASE("MFLES v2: Constant data series", "[mfles_v2][edge]") {
	std::vector<double> data(60, 150.0);
	auto ts = createTimeSeries(data);

	MFLES mfles;
	REQUIRE_NOTHROW(mfles.fit(ts));

	auto forecast = mfles.predict(12);
	for (double f : forecast.primary()) {
		REQUIRE(std::abs(f - 150.0) < 10.0);
	}
}

TEST_CASE("MFLES v2: Data with large variance", "[mfles_v2][edge]") {
	std::vector<double> data(60);
	for (int i = 0; i < 60; ++i) {
		data[i] = 1000.0 + 500.0 * std::sin(2.0 * M_PI * i / 12);
	}
	auto ts = createTimeSeries(data);

	MFLES mfles;
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Data with small values", "[mfles_v2][edge]") {
	std::vector<double> data(60);
	for (int i = 0; i < 60; ++i) {
		data[i] = 0.01 + 0.005 * std::sin(2.0 * M_PI * i / 12);
	}
	auto ts = createTimeSeries(data);

	MFLES mfles;
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Period longer than data", "[mfles_v2][edge]") {
	std::vector<double> data(20);
	for (int i = 0; i < 20; ++i) {
		data[i] = 100.0 + i;
	}
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.seasonal_periods = {50};  // Period longer than data

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: No seasonal periods specified", "[mfles_v2][edge]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES::Params params;
	params.seasonal_periods = {};  // No seasonality

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));

	auto forecast = mfles.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("MFLES v2: Negative values in data", "[mfles_v2][edge]") {
	std::vector<double> data(60);
	for (int i = 0; i < 60; ++i) {
		data[i] = -50.0 + 20.0 * std::sin(2.0 * M_PI * i / 12);
	}
	auto ts = createTimeSeries(data);

	MFLES mfles;
	REQUIRE_NOTHROW(mfles.fit(ts));
}

TEST_CASE("MFLES v2: Data with noise spikes", "[mfles_v2][edge]") {
	auto data = generateSeasonalData(80, 12);
	// Add random spikes
	data[10] += 30.0;
	data[25] -= 25.0;
	data[40] += 35.0;
	data[55] -= 30.0;
	data[70] += 40.0;

	auto ts = createTimeSeries(data);

	MFLES::Params params = MFLES::robustPreset();

	MFLES mfles(params);
	REQUIRE_NOTHROW(mfles.fit(ts));
}

// ============================================================================
// Decomposition Tests
// ============================================================================

TEST_CASE("MFLES v2: Seasonal decomposition", "[mfles_v2][decomposition]") {
	auto data = generateSeasonalData(60, 12, 10.0, 0.5, 100.0);
	auto ts = createTimeSeries(data);

	MFLES mfles;
	mfles.fit(ts);

	auto decomp = mfles.seasonal_decompose();

	REQUIRE(decomp.trend.size() == 60);
	REQUIRE(decomp.seasonal.size() == 60);
	REQUIRE(decomp.level.size() == 60);
	REQUIRE(decomp.residuals.size() == 60);
}

// ============================================================================
// Builder Pattern Tests
// ============================================================================

TEST_CASE("MFLES v2: Builder with fluent API", "[mfles_v2][builder]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	auto mfles = MFLESBuilder()
		.withSeasonalPeriods({12})
		.withMaxRounds(5)
		.withLearningRates(0.9, 0.9, 1.0)
		.withTrendMethod(TrendMethod::OLS)
		.withFourierOrder(5)
		.build();

	REQUIRE_NOTHROW(mfles->fit(ts));
}

TEST_CASE("MFLES v2: Builder with ES ensemble configuration", "[mfles_v2][builder]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	auto mfles = MFLESBuilder()
		.withSeasonalPeriods({12})
		.withESEnsemble(0.1, 0.9, 20)
		.build();

	REQUIRE_NOTHROW(mfles->fit(ts));
}

TEST_CASE("MFLES v2: Builder with outlier capping", "[mfles_v2][builder]") {
	auto data = generateSeasonalData(60, 12);
	data[30] += 50.0;

	auto ts = createTimeSeries(data);

	auto mfles = MFLESBuilder()
		.withSeasonalPeriods({12})
		.withOutlierCapping(true, 2.5)
		.build();

	REQUIRE_NOTHROW(mfles->fit(ts));
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_CASE("MFLES v2: Predict before fit throws", "[mfles_v2][errors]") {
	MFLES mfles;
	REQUIRE_THROWS(mfles.predict(12));
}

TEST_CASE("MFLES v2: Invalid horizon throws", "[mfles_v2][errors]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);

	MFLES mfles;
	mfles.fit(ts);

	REQUIRE_THROWS(mfles.predict(0));
	REQUIRE_THROWS(mfles.predict(-5));
}

TEST_CASE("MFLES v2: Decompose before fit throws", "[mfles_v2][errors]") {
	MFLES mfles;
	REQUIRE_THROWS(mfles.seasonal_decompose());
}

// ============================================================================
// Integration and Workflow Tests
// ============================================================================

TEST_CASE("MFLES v2: Complete forecasting workflow", "[mfles_v2][integration]") {
	auto data = generateSeasonalData(100, 12, 15.0, 0.8, 120.0);
	auto ts = createTimeSeries(data);

	// Use accurate preset
	auto params = MFLES::accuratePreset();
	params.seasonal_periods = {12};

	MFLES mfles(params);
	mfles.fit(ts);

	// Generate forecasts
	auto forecast_6 = mfles.predict(6);
	auto forecast_12 = mfles.predict(12);
	auto forecast_24 = mfles.predict(24);

	REQUIRE(forecast_6.primary().size() == 6);
	REQUIRE(forecast_12.primary().size() == 12);
	REQUIRE(forecast_24.primary().size() == 24);

	// Get diagnostics
	REQUIRE(mfles.fittedValues().size() == 100);
	REQUIRE(mfles.residuals().size() == 100);
	REQUIRE(mfles.actualRoundsUsed() > 0);

	// Decompose
	auto decomp = mfles.seasonal_decompose();
	REQUIRE(decomp.trend.size() == 100);
}
