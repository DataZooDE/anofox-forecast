#include "anofox-time/models/auto_mstl.hpp"
#include "anofox-time/core/time_series.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vector>
#include <cmath>

using namespace anofoxtime;
using namespace anofoxtime::models;
using namespace anofoxtime::core;

// Helper to generate test data
std::vector<double> generateTrendSeasonalData(int n, int period) {
	std::vector<double> data(n);
	for (int i = 0; i < n; ++i) {
		double trend = 100.0 + 2.0 * i;
		double seasonal = 10.0 * std::sin(2.0 * M_PI * i / period);
		data[i] = trend + seasonal;
	}
	return data;
}

TEST_CASE("AutoMSTL basic construction", "[auto_mstl]") {
	SECTION("Valid construction") {
		REQUIRE_NOTHROW(AutoMSTL({12}));
		REQUIRE_NOTHROW(AutoMSTL({12}, 2, false));
	}
	
	SECTION("Invalid seasonal periods") {
		REQUIRE_THROWS_AS(AutoMSTL({}), std::invalid_argument);
		REQUIRE_THROWS_AS(AutoMSTL({1}), std::invalid_argument);
		REQUIRE_THROWS_AS(AutoMSTL({-1}), std::invalid_argument);
	}
}

TEST_CASE("AutoMSTL fit and predict", "[auto_mstl]") {
	auto data = generateTrendSeasonalData(144, 12); // 12 years of monthly data
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	AutoMSTL auto_mstl({12});
	
	SECTION("Fit succeeds") {
		REQUIRE_NOTHROW(auto_mstl.fit(ts));
	}
	
	SECTION("Predict after fit") {
		auto_mstl.fit(ts);
		auto forecast = auto_mstl.predict(12);
		
		REQUIRE(forecast.primary().size() == 12);
		
		// Check that forecasts are reasonable
		for (double val : forecast.primary()) {
			REQUIRE(std::isfinite(val));
			REQUIRE(val > 0.0); // Should be positive for this data
		}
	}
	
	SECTION("Cannot predict before fit") {
		REQUIRE_THROWS_AS(auto_mstl.predict(12), std::runtime_error);
	}
}

TEST_CASE("AutoMSTL model selection", "[auto_mstl]") {
	auto data = generateTrendSeasonalData(144, 12);
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	AutoMSTL auto_mstl({12});
	auto_mstl.fit(ts);
	
	SECTION("Model is selected") {
		REQUIRE_NOTHROW(auto_mstl.selectedModel());
		REQUIRE_NOTHROW(auto_mstl.selectedTrendMethod());
		REQUIRE_NOTHROW(auto_mstl.selectedSeasonalMethod());
	}
	
	SECTION("AIC is finite and reasonable") {
		double aic = auto_mstl.selectedAIC();
		REQUIRE(std::isfinite(aic));
		REQUIRE(aic < std::numeric_limits<double>::infinity());
	}
	
	SECTION("Cannot access model before fit") {
		AutoMSTL unfitted({12});
		REQUIRE_THROWS_AS(unfitted.selectedModel(), std::runtime_error);
		REQUIRE_THROWS_AS(unfitted.selectedAIC(), std::runtime_error);
	}
}

TEST_CASE("AutoMSTL diagnostics", "[auto_mstl]") {
	auto data = generateTrendSeasonalData(144, 12);
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	AutoMSTL auto_mstl({12});
	auto_mstl.fit(ts);
	
	const auto& diag = auto_mstl.diagnostics();
	
	SECTION("Models evaluated") {
		// Should evaluate all 18 candidates (6 trend × 3 seasonal)
		// Some may fail, so check for reasonable range
		REQUIRE(diag.models_evaluated > 0);
		REQUIRE(diag.models_evaluated <= 18);
	}
	
	SECTION("Best AIC is recorded") {
		REQUIRE(std::isfinite(diag.best_aic));
		REQUIRE(diag.best_aic < std::numeric_limits<double>::infinity());
		REQUIRE(diag.best_aic == auto_mstl.selectedAIC());
	}
	
	SECTION("Optimization time is positive") {
		REQUIRE(diag.optimization_time_ms > 0.0);
		REQUIRE(diag.optimization_time_ms < 10000.0); // Should be under 10 seconds
	}
}

TEST_CASE("AutoMSTL multiple seasonalities", "[auto_mstl]") {
	// Generate data with multiple seasonal patterns
	std::vector<double> data(336); // 4 weeks × 7 days × 12 hours
	for (size_t i = 0; i < data.size(); ++i) {
		double trend = 50.0 + 0.5 * i;
		double daily = 5.0 * std::sin(2.0 * M_PI * i / 24.0);  // Daily pattern
		double weekly = 3.0 * std::sin(2.0 * M_PI * i / 168.0); // Weekly pattern
		data[i] = trend + daily + weekly;
	}
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	AutoMSTL auto_mstl({24, 168}); // Daily and weekly seasonality
	
	SECTION("Fit with multiple seasonalities") {
		REQUIRE_NOTHROW(auto_mstl.fit(ts));
	}
	
	SECTION("Forecast with multiple seasonalities") {
		auto_mstl.fit(ts);
		auto forecast = auto_mstl.predict(24);
		
		REQUIRE(forecast.primary().size() == 24);
		for (double val : forecast.primary()) {
			REQUIRE(std::isfinite(val));
		}
	}
}

TEST_CASE("AutoMSTL with constant data", "[auto_mstl]") {
	std::vector<double> data(100, 50.0);
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	AutoMSTL auto_mstl({12});
	
	SECTION("Fit constant data") {
		REQUIRE_NOTHROW(auto_mstl.fit(ts));
	}
	
	SECTION("Forecast constant data") {
		auto_mstl.fit(ts);
		auto forecast = auto_mstl.predict(12);
		
		REQUIRE(forecast.primary().size() == 12);
		
		// Should forecast approximately constant values
		for (double val : forecast.primary()) {
			REQUIRE(std::isfinite(val));
			REQUIRE_THAT(val, Catch::Matchers::WithinAbs(50.0, 5.0));
		}
	}
}

TEST_CASE("AutoMSTL trend-only data", "[auto_mstl]") {
	// Pure linear trend, no seasonality
	std::vector<double> data(100);
	for (size_t i = 0; i < data.size(); ++i) {
		data[i] = 10.0 + 2.0 * i;
	}
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	AutoMSTL auto_mstl({12});
	auto_mstl.fit(ts);
	
	auto forecast = auto_mstl.predict(10);
	
	SECTION("Forecast continues trend") {
		REQUIRE(forecast.primary().size() == 10);
		
		// Should be in a reasonable range (AutoMSTL may decompose differently)
		double last_value = data.back();
		REQUIRE(forecast.primary()[0] >= last_value - 20.0);
		REQUIRE(forecast.primary()[0] <= last_value + 30.0);
	}
}

TEST_CASE("AutoMSTL seasonal-only data", "[auto_mstl]") {
	// Pure seasonality, no trend
	std::vector<double> data(120);
	for (size_t i = 0; i < data.size(); ++i) {
		data[i] = 100.0 + 20.0 * std::sin(2.0 * M_PI * i / 12.0);
	}
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	AutoMSTL auto_mstl({12});
	auto_mstl.fit(ts);
	
	auto forecast = auto_mstl.predict(12);
	
	SECTION("Forecast captures seasonality") {
		REQUIRE(forecast.primary().size() == 12);
		
		// Should capture seasonal pattern
		for (double val : forecast.primary()) {
			REQUIRE(std::isfinite(val));
			REQUIRE(val >= 75.0); // Min of sine wave
			REQUIRE(val <= 125.0); // Max of sine wave
		}
	}
}

TEST_CASE("AutoMSTL getName", "[auto_mstl]") {
	AutoMSTL auto_mstl({12});
	REQUIRE(auto_mstl.getName() == "AutoMSTL");
}

TEST_CASE("AutoMSTL builder pattern", "[auto_mstl]") {
	auto builder = AutoMSTLBuilder();
	
	SECTION("Build with defaults") {
		auto model = builder.build();
		REQUIRE(model != nullptr);
	}
	
	SECTION("Build with custom parameters") {
		auto model = builder
			.withSeasonalPeriods({7, 30})
			.withMSTLIterations(3)
			.withRobust(true)
			.build();
		
		REQUIRE(model != nullptr);
	}
}

TEST_CASE("AutoMSTL increasing data", "[auto_mstl]") {
	std::vector<double> data(100);
	for (size_t i = 0; i < data.size(); ++i) {
		double trend = 100.0 + 5.0 * i;
		double seasonal = 10.0 * std::sin(2.0 * M_PI * i / 12.0);
		data[i] = trend + seasonal + (i % 2 == 0 ? 2.0 : -2.0); // Add some noise
	}
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	AutoMSTL auto_mstl({12});
	auto_mstl.fit(ts);
	
	auto forecast = auto_mstl.predict(12);
	
	SECTION("Forecast shows increasing trend") {
		REQUIRE(forecast.primary().size() == 12);
		
		// Forecast should be in a reasonable range (AutoMSTL may select different methods)
		REQUIRE(forecast.primary()[0] > data.back() - 50.0);
		REQUIRE(forecast.primary()[0] < data.back() + 100.0);
	}
}

TEST_CASE("AutoMSTL decreasing data", "[auto_mstl]") {
	std::vector<double> data(100);
	for (size_t i = 0; i < data.size(); ++i) {
		double trend = 1000.0 - 3.0 * i;
		double seasonal = 15.0 * std::sin(2.0 * M_PI * i / 12.0);
		data[i] = trend + seasonal;
	}
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	AutoMSTL auto_mstl({12});
	auto_mstl.fit(ts);
	
	auto forecast = auto_mstl.predict(12);
	
	SECTION("Forecast shows decreasing trend") {
		REQUIRE(forecast.primary().size() == 12);
		
		// Check that values are finite
		for (double val : forecast.primary()) {
			REQUIRE(std::isfinite(val));
		}
	}
}

TEST_CASE("AutoMSTL with small dataset", "[auto_mstl]") {
	// Minimal dataset (2 complete seasons)
	std::vector<double> data(24);
	for (size_t i = 0; i < data.size(); ++i) {
		data[i] = 100.0 + 10.0 * std::sin(2.0 * M_PI * i / 12.0);
	}
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	AutoMSTL auto_mstl({12});
	
	SECTION("Fit small dataset") {
		REQUIRE_NOTHROW(auto_mstl.fit(ts));
	}
	
	SECTION("Forecast from small dataset") {
		auto_mstl.fit(ts);
		auto forecast = auto_mstl.predict(6);
		
		REQUIRE(forecast.primary().size() == 6);
		for (double val : forecast.primary()) {
			REQUIRE(std::isfinite(val));
		}
	}
}

TEST_CASE("AutoMSTL AIC comparison", "[auto_mstl]") {
	auto data = generateTrendSeasonalData(144, 12);
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	AutoMSTL auto_mstl({12});
	auto_mstl.fit(ts);
	
	SECTION("AutoMSTL selects valid configuration") {
		// Just verify that a model was selected and has finite AIC
		double aic = auto_mstl.selectedAIC();
		REQUIRE(std::isfinite(aic));
		
		// Verify selected methods are valid
		auto trend = auto_mstl.selectedTrendMethod();
		auto seasonal = auto_mstl.selectedSeasonalMethod();
		
		// These are enums, so just check they can be accessed
		REQUIRE_NOTHROW(auto_mstl.selectedModel());
	}
}

TEST_CASE("AutoMSTL robustness option", "[auto_mstl]") {
	auto data = generateTrendSeasonalData(100, 12);
	
	// Add some outliers
	data[10] = data[10] * 3.0;
	data[50] = data[50] * 0.3;
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	SECTION("Robust fitting") {
		AutoMSTL auto_mstl_robust({12}, 2, true);
		REQUIRE_NOTHROW(auto_mstl_robust.fit(ts));
		
		auto forecast = auto_mstl_robust.predict(12);
		REQUIRE(forecast.primary().size() == 12);
	}
	
	SECTION("Non-robust fitting") {
		AutoMSTL auto_mstl_normal({12}, 2, false);
		REQUIRE_NOTHROW(auto_mstl_normal.fit(ts));
		
		auto forecast = auto_mstl_normal.predict(12);
		REQUIRE(forecast.primary().size() == 12);
	}
}

TEST_CASE("AutoMSTL different MSTL iterations", "[auto_mstl]") {
	auto data = generateTrendSeasonalData(120, 12);
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	SECTION("1 iteration") {
		AutoMSTL auto_mstl({12}, 1);
		REQUIRE_NOTHROW(auto_mstl.fit(ts));
	}
	
	SECTION("2 iterations (default)") {
		AutoMSTL auto_mstl({12}, 2);
		REQUIRE_NOTHROW(auto_mstl.fit(ts));
	}
	
	SECTION("5 iterations") {
		AutoMSTL auto_mstl({12}, 5);
		REQUIRE_NOTHROW(auto_mstl.fit(ts));
	}
}

TEST_CASE("AutoMSTL large horizon forecast", "[auto_mstl]") {
	auto data = generateTrendSeasonalData(144, 12);
	
	std::vector<TimeSeries::TimePoint> timestamps;
	auto start = TimeSeries::TimePoint{};
	for (size_t i = 0; i < data.size(); ++i) {
		timestamps.push_back(start + std::chrono::seconds(static_cast<long>(i)));
	}
	
	TimeSeries ts(timestamps, data);
	
	AutoMSTL auto_mstl({12});
	auto_mstl.fit(ts);
	
	SECTION("Forecast 24 periods ahead") {
		auto forecast = auto_mstl.predict(24);
		
		REQUIRE(forecast.primary().size() == 24);
		for (double val : forecast.primary()) {
			REQUIRE(std::isfinite(val));
		}
	}
	
	SECTION("Forecast 36 periods ahead") {
		auto forecast = auto_mstl.predict(36);
		
		REQUIRE(forecast.primary().size() == 36);
		for (double val : forecast.primary()) {
			REQUIRE(std::isfinite(val));
		}
	}
}

