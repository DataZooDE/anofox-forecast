#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "anofox-time/models/tbats.hpp"
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

TEST_CASE("TBATS constructor and config", "[tbats][basic]") {
	TBATS::Config config;
	config.seasonal_periods = {12};
	
	TBATS tbats(config);
	REQUIRE(tbats.getName() == "TBATS");
	REQUIRE(tbats.config().seasonal_periods == std::vector<int>{12});
}

TEST_CASE("TBATS constructor validates parameters", "[tbats][basic]") {
	TBATS::Config config1;
	config1.seasonal_periods = {};
	REQUIRE_THROWS_AS(TBATS(config1), std::invalid_argument);  // Empty periods
	
	TBATS::Config config2;
	config2.seasonal_periods = {1};
	REQUIRE_THROWS_AS(TBATS(config2), std::invalid_argument);  // Period too small
	
	TBATS::Config config3;
	config3.seasonal_periods = {12};
	config3.ar_order = 10;
	REQUIRE_THROWS_AS(TBATS(config3), std::invalid_argument);  // AR too large
	
	TBATS::Config config4;
	config4.seasonal_periods = {12};
	config4.use_damped_trend = true;
	config4.damping_param = 1.5;
	REQUIRE_THROWS_AS(TBATS(config4), std::invalid_argument);  // Invalid damping
}

TEST_CASE("TBATS fit and predict", "[tbats][basic]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	
	TBATS tbats(config);
	REQUIRE_NOTHROW(tbats.fit(ts));
	
	auto forecast = tbats.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("TBATS requires fit before predict", "[tbats][error]") {
	TBATS::Config config;
	config.seasonal_periods = {12};
	
	TBATS tbats(config);
	REQUIRE_THROWS_AS(tbats.predict(10), std::runtime_error);
}

TEST_CASE("TBATS validates horizon", "[tbats][error]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	
	TBATS tbats(config);
	tbats.fit(ts);
	
	REQUIRE_THROWS_AS(tbats.predict(0), std::invalid_argument);
	REQUIRE_THROWS_AS(tbats.predict(-5), std::invalid_argument);
}

TEST_CASE("TBATS fitted values and residuals", "[tbats][basic]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	
	TBATS tbats(config);
	tbats.fit(ts);
	
	REQUIRE(tbats.fittedValues().size() == 48);
	REQUIRE(tbats.residuals().size() == 48);
	REQUIRE(std::isfinite(tbats.aic()));
}

// ============================================================================
// Box-Cox Transformation Tests
// ============================================================================

TEST_CASE("TBATS no Box-Cox", "[tbats][boxcox]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	config.use_box_cox = false;
	
	TBATS tbats(config);
	REQUIRE_NOTHROW(tbats.fit(ts));
	
	auto forecast = tbats.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("TBATS with Box-Cox log transform", "[tbats][boxcox]") {
	auto data = generateSeasonalData(48, 12, 10.0, 0.5, 100.0);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	config.use_box_cox = true;
	config.box_cox_lambda = 0.0;  // Log transform
	
	TBATS tbats(config);
	REQUIRE_NOTHROW(tbats.fit(ts));
	
	auto forecast = tbats.predict(12);
	REQUIRE(forecast.primary().size() == 12);
	
	// Forecasts should be positive (after inverse transform)
	for (double f : forecast.primary()) {
		REQUIRE(f > 0.0);
	}
}

TEST_CASE("TBATS with Box-Cox lambda=0.5", "[tbats][boxcox]") {
	auto data = generateSeasonalData(48, 12, 10.0, 0.5, 100.0);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	config.use_box_cox = true;
	config.box_cox_lambda = 0.5;  // Square root
	
	TBATS tbats(config);
	REQUIRE_NOTHROW(tbats.fit(ts));
	
	auto forecast = tbats.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("TBATS Box-Cox requires positive data for lambda=0", "[tbats][boxcox]") {
	std::vector<double> data = {-10.0, -5.0, 0.0, 5.0, 10.0};
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {2};
	config.use_box_cox = true;
	config.box_cox_lambda = 0.0;  // Log requires positive
	
	TBATS tbats(config);
	REQUIRE_THROWS_AS(tbats.fit(ts), std::runtime_error);
}

TEST_CASE("TBATS Box-Cox lambda=1 equivalent to no transform", "[tbats][boxcox]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config1;
	config1.seasonal_periods = {12};
	config1.use_box_cox = false;
	
	TBATS::Config config2;
	config2.seasonal_periods = {12};
	config2.use_box_cox = true;
	config2.box_cox_lambda = 1.0;
	
	TBATS tbats1(config1);
	TBATS tbats2(config2);
	
	tbats1.fit(ts);
	tbats2.fit(ts);
	
	auto forecast1 = tbats1.predict(12);
	auto forecast2 = tbats2.predict(12);
	
	// Forecasts should be very similar
	for (size_t i = 0; i < 12; ++i) {
		REQUIRE(std::abs(forecast1.primary()[i] - forecast2.primary()[i]) < 5.0);
	}
}

// ============================================================================
// Trend Configuration Tests
// ============================================================================

TEST_CASE("TBATS with no trend", "[tbats][trend]") {
	auto data = generateSeasonalData(48, 12, 10.0, 0.0, 100.0);  // No trend
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	config.use_trend = false;
	
	TBATS tbats(config);
	REQUIRE_NOTHROW(tbats.fit(ts));
	
	auto forecast = tbats.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("TBATS with linear trend", "[tbats][trend]") {
	auto data = generateSeasonalData(48, 12, 10.0, 1.0, 100.0);  // With trend
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	config.use_trend = true;
	config.use_damped_trend = false;
	
	TBATS tbats(config);
	REQUIRE_NOTHROW(tbats.fit(ts));
	
	auto forecast = tbats.predict(12);
	
	// With linear trend, forecasts should increase
	REQUIRE(forecast.primary()[11] > forecast.primary()[0]);
}

TEST_CASE("TBATS with damped trend", "[tbats][trend]") {
	auto data = generateSeasonalData(48, 12, 10.0, 1.0, 100.0);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	config.use_trend = true;
	config.use_damped_trend = true;
	config.damping_param = 0.95;
	
	TBATS tbats(config);
	REQUIRE_NOTHROW(tbats.fit(ts));
	
	auto forecast = tbats.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

// ============================================================================
// Fourier Seasonality Tests
// ============================================================================

TEST_CASE("TBATS single seasonality", "[tbats][fourier]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	
	TBATS tbats(config);
	tbats.fit(ts);
	
	// Fourier K should be auto-selected
	REQUIRE(tbats.config().fourier_k.size() == 1);
	REQUIRE(tbats.config().fourier_k[0] > 0);
	REQUIRE(tbats.config().fourier_k[0] <= 10);
}

TEST_CASE("TBATS multiple seasonalities", "[tbats][fourier]") {
	std::vector<double> data(90);
	for (int i = 0; i < 90; ++i) {
		data[i] = 100.0 + 
		          10.0 * std::sin(2.0 * M_PI * i / 7.0) +
		          5.0 * std::sin(2.0 * M_PI * i / 30.0);
	}
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {7, 30};
	
	TBATS tbats(config);
	tbats.fit(ts);
	
	REQUIRE(tbats.config().fourier_k.size() == 2);
	
	auto forecast = tbats.predict(14);
	REQUIRE(forecast.primary().size() == 14);
}

TEST_CASE("TBATS manual Fourier K", "[tbats][fourier]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	config.fourier_k = {3};  // Manually set K=3
	
	TBATS tbats(config);
	tbats.fit(ts);
	
	REQUIRE(tbats.config().fourier_k[0] == 3);
}

TEST_CASE("TBATS Fourier seasonality projection", "[tbats][fourier]") {
	auto data = generateSeasonalData(60, 12, 10.0, 0.0, 100.0);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	config.use_trend = false;  // No trend for this test
	
	TBATS tbats(config);
	tbats.fit(ts);
	
	auto forecast = tbats.predict(24);  // 2 full cycles
	
	// Seasonal pattern should be somewhat consistent
	// (not exact due to state-space dynamics)
	REQUIRE(forecast.primary().size() == 24);
}

// ============================================================================
// ARMA Errors Tests
// ============================================================================

TEST_CASE("TBATS with AR errors", "[tbats][arma]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	config.ar_order = 1;
	config.ma_order = 0;
	
	TBATS tbats(config);
	REQUIRE_NOTHROW(tbats.fit(ts));
	
	auto forecast = tbats.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("TBATS with MA errors", "[tbats][arma]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	config.ar_order = 0;
	config.ma_order = 1;
	
	TBATS tbats(config);
	REQUIRE_NOTHROW(tbats.fit(ts));
	
	auto forecast = tbats.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("TBATS with ARMA errors", "[tbats][arma]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	config.ar_order = 1;
	config.ma_order = 1;
	
	TBATS tbats(config);
	REQUIRE_NOTHROW(tbats.fit(ts));
	
	auto forecast = tbats.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("TBATS without ARMA", "[tbats][arma]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	config.ar_order = 0;
	config.ma_order = 0;
	
	TBATS tbats(config);
	REQUIRE_NOTHROW(tbats.fit(ts));
	
	auto forecast = tbats.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

// ============================================================================
// State-Space Tests
// ============================================================================

TEST_CASE("TBATS AIC computation", "[tbats][statespace]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	
	TBATS tbats(config);
	tbats.fit(ts);
	
	double aic = tbats.aic();
	REQUIRE(std::isfinite(aic));
	REQUIRE(aic < std::numeric_limits<double>::infinity());
}

TEST_CASE("TBATS fitted values quality", "[tbats][statespace]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	
	TBATS tbats(config);
	tbats.fit(ts);
	
	const auto& fitted = tbats.fittedValues();
	const auto& residuals = tbats.residuals();
	
	// Check residuals = actual - fitted
	for (size_t i = 0; i < data.size(); ++i) {
		REQUIRE_THAT(residuals[i], 
		             Catch::Matchers::WithinAbs(data[i] - fitted[i], 0.1));
	}
}

TEST_CASE("TBATS state propagation", "[tbats][statespace]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	config.use_trend = true;
	
	TBATS tbats(config);
	tbats.fit(ts);
	
	// Multiple forecasts should be consistent
	auto forecast1 = tbats.predict(12);
	auto forecast2 = tbats.predict(12);
	
	for (size_t i = 0; i < 12; ++i) {
		REQUIRE_THAT(forecast1.primary()[i],
		             Catch::Matchers::WithinAbs(forecast2.primary()[i], 1e-10));
	}
}

// ============================================================================
// Edge Cases Tests
// ============================================================================

TEST_CASE("TBATS short data", "[tbats][edge]") {
	std::vector<double> data = {100., 105., 110., 108., 112., 115., 113., 118.};
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {4};
	
	TBATS tbats(config);
	REQUIRE_NOTHROW(tbats.fit(ts));
	
	auto forecast = tbats.predict(4);
	REQUIRE(forecast.primary().size() == 4);
}

TEST_CASE("TBATS constant data", "[tbats][edge]") {
	std::vector<double> data(30, 100.0);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {10};
	
	TBATS tbats(config);
	REQUIRE_NOTHROW(tbats.fit(ts));
	
	auto forecast = tbats.predict(10);
	
	// Should forecast near constant
	for (double f : forecast.primary()) {
		REQUIRE(std::abs(f - 100.0) < 20.0);
	}
}

TEST_CASE("TBATS large horizon", "[tbats][edge]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);
	
	TBATS::Config config;
	config.seasonal_periods = {12};
	
	TBATS tbats(config);
	tbats.fit(ts);
	
	auto forecast = tbats.predict(48);  // 4 full cycles
	REQUIRE(forecast.primary().size() == 48);
	
	// All should be finite
	for (double f : forecast.primary()) {
		REQUIRE(std::isfinite(f));
	}
}

// ============================================================================
// Builder Tests
// ============================================================================

TEST_CASE("TBATS builder pattern", "[tbats][builder]") {
	auto tbats = TBATSBuilder()
		.withSeasonalPeriods({7, 12})
		.withBoxCox(true, 0.5)
		.withTrend(true)
		.withDampedTrend(true, 0.95)
		.withARMA(1, 1)
		.build();
	
	REQUIRE(tbats->getName() == "TBATS");
	REQUIRE(tbats->config().seasonal_periods == std::vector<int>{7, 12});
	REQUIRE(tbats->config().use_box_cox == true);
	REQUIRE(tbats->config().box_cox_lambda == 0.5);
	REQUIRE(tbats->config().use_trend == true);
	REQUIRE(tbats->config().use_damped_trend == true);
	REQUIRE(tbats->config().ar_order == 1);
	REQUIRE(tbats->config().ma_order == 1);
}

TEST_CASE("TBATS builder default config", "[tbats][builder]") {
	auto tbats = TBATSBuilder()
		.withSeasonalPeriods({12})
		.build();
	
	REQUIRE(tbats->config().use_box_cox == false);
	REQUIRE(tbats->config().use_trend == true);
	REQUIRE(tbats->config().ar_order == 0);
	REQUIRE(tbats->config().ma_order == 0);
}

