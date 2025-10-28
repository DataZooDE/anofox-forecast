#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "anofox-time/models/auto_tbats.hpp"
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

TEST_CASE("AutoTBATS constructor", "[auto_tbats][basic]") {
	AutoTBATS auto_tbats({12});
	
	REQUIRE(auto_tbats.getName() == "AutoTBATS");
}

TEST_CASE("AutoTBATS constructor validates parameters", "[auto_tbats][basic]") {
	REQUIRE_THROWS_AS(AutoTBATS({}), std::invalid_argument);  // Empty periods
	REQUIRE_THROWS_AS(AutoTBATS({1}), std::invalid_argument);  // Period too small
}

TEST_CASE("AutoTBATS fit and predict", "[auto_tbats][basic]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	AutoTBATS auto_tbats({12});
	REQUIRE_NOTHROW(auto_tbats.fit(ts));
	
	auto forecast = auto_tbats.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("AutoTBATS requires fit before predict", "[auto_tbats][error]") {
	AutoTBATS auto_tbats({12});
	REQUIRE_THROWS_AS(auto_tbats.predict(10), std::runtime_error);
}

TEST_CASE("AutoTBATS requires fit before accessing model", "[auto_tbats][error]") {
	AutoTBATS auto_tbats({12});
	REQUIRE_THROWS_AS(auto_tbats.selectedModel(), std::runtime_error);
	REQUIRE_THROWS_AS(auto_tbats.selectedConfig(), std::runtime_error);
	REQUIRE_THROWS_AS(auto_tbats.selectedAIC(), std::runtime_error);
}

// ============================================================================
// Optimization Tests
// ============================================================================

TEST_CASE("AutoTBATS optimizes parameters", "[auto_tbats][optimization]") {
	auto data = generateSeasonalData(48, 12, 10.0, 0.5, 100.0);
	auto ts = createTimeSeries(data);
	
	AutoTBATS auto_tbats({12});
	auto_tbats.fit(ts);
	
	// Should have evaluated multiple models
	REQUIRE(auto_tbats.diagnostics().models_evaluated > 5);
	
	// Selected config should be valid
	const auto& config = auto_tbats.selectedConfig();
	REQUIRE(!config.seasonal_periods.empty());
	REQUIRE(config.seasonal_periods[0] == 12);
}

TEST_CASE("AutoTBATS diagnostics populated", "[auto_tbats][diagnostics]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	AutoTBATS auto_tbats({12});
	auto_tbats.fit(ts);
	
	const auto& diag = auto_tbats.diagnostics();
	
	REQUIRE(diag.models_evaluated > 0);
	REQUIRE(std::isfinite(diag.best_aic));
	REQUIRE(diag.best_aic < std::numeric_limits<double>::infinity());
	REQUIRE(diag.optimization_time_ms > 0.0);
}

TEST_CASE("AutoTBATS selected model accessible", "[auto_tbats][model]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	AutoTBATS auto_tbats({12});
	auto_tbats.fit(ts);
	
	const auto& model = auto_tbats.selectedModel();
	
	REQUIRE(model.getName() == "TBATS");
	REQUIRE(model.fittedValues().size() == 48);
	REQUIRE(model.residuals().size() == 48);
}

TEST_CASE("AutoTBATS selects reasonable config", "[auto_tbats][optimization]") {
	auto data = generateSeasonalData(60, 12, 10.0, 1.0, 100.0);
	auto ts = createTimeSeries(data);
	
	AutoTBATS auto_tbats({12});
	auto_tbats.fit(ts);
	
	const auto& config = auto_tbats.selectedConfig();
	
	// Should have selected some configuration
	REQUIRE(config.seasonal_periods.size() == 1);
	REQUIRE(config.seasonal_periods[0] == 12);
	
	// AIC should be finite
	REQUIRE(std::isfinite(auto_tbats.selectedAIC()));
}

// ============================================================================
// Multiple Seasonalities Tests
// ============================================================================

TEST_CASE("AutoTBATS handles multiple periods", "[auto_tbats][seasonality]") {
	std::vector<double> data(72);
	for (int i = 0; i < 72; ++i) {
		data[i] = 100.0 + 
		          10.0 * std::sin(2.0 * M_PI * i / 12.0) +
		          5.0 * std::sin(2.0 * M_PI * i / 4.0);
	}
	auto ts = createTimeSeries(data);
	
	AutoTBATS auto_tbats({12, 4});
	REQUIRE_NOTHROW(auto_tbats.fit(ts));
	
	auto forecast = auto_tbats.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_CASE("AutoTBATS forecast quality", "[auto_tbats][quality]") {
	auto data = generateSeasonalData(60, 12, 10.0, 0.5, 100.0);
	
	std::vector<double> train_data(data.begin(), data.begin() + 48);
	std::vector<double> test_data(data.begin() + 48, data.end());
	
	auto train_ts = createTimeSeries(train_data);
	
	AutoTBATS auto_tbats({12});
	auto_tbats.fit(train_ts);
	auto forecast = auto_tbats.predict(12);
	
	// Calculate MAE
	double mae = 0.0;
	for (size_t i = 0; i < test_data.size(); ++i) {
		mae += std::abs(forecast.primary()[i] - test_data[i]);
	}
	mae /= test_data.size();
	
	// Should have reasonable accuracy
	REQUIRE(mae < 25.0);
}

TEST_CASE("AutoTBATS vs manual TBATS", "[auto_tbats][comparison]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	AutoTBATS auto_tbats({12});
	auto_tbats.fit(ts);
	
	// Create manual TBATS with selected config
	TBATS manual_tbats(auto_tbats.selectedConfig());
	manual_tbats.fit(ts);
	
	// Forecasts should be identical
	auto forecast_auto = auto_tbats.predict(12);
	auto forecast_manual = manual_tbats.predict(12);
	
	for (size_t i = 0; i < 12; ++i) {
		REQUIRE_THAT(forecast_auto.primary()[i],
		             Catch::Matchers::WithinAbs(forecast_manual.primary()[i], 1e-6));
	}
}

// ============================================================================
// Edge Cases Tests
// ============================================================================

TEST_CASE("AutoTBATS constant data", "[auto_tbats][edge]") {
	std::vector<double> data(30, 100.0);
	auto ts = createTimeSeries(data);
	
	AutoTBATS auto_tbats({10});
	REQUIRE_NOTHROW(auto_tbats.fit(ts));
	
	auto forecast = auto_tbats.predict(10);
	
	// Should forecast near constant
	for (double f : forecast.primary()) {
		REQUIRE(std::abs(f - 100.0) < 25.0);
	}
}

TEST_CASE("AutoTBATS short data", "[auto_tbats][edge]") {
	std::vector<double> data = {100., 105., 110., 108., 112., 115., 113., 118., 120., 122.};
	auto ts = createTimeSeries(data);
	
	AutoTBATS auto_tbats({5});
	REQUIRE_NOTHROW(auto_tbats.fit(ts));
	
	auto forecast = auto_tbats.predict(5);
	REQUIRE(forecast.primary().size() == 5);
}

// ============================================================================
// Builder Tests
// ============================================================================

TEST_CASE("AutoTBATS builder pattern", "[auto_tbats][builder]") {
	auto auto_tbats = AutoTBATSBuilder()
		.withSeasonalPeriods({7, 12})
		.build();
	
	REQUIRE(auto_tbats->getName() == "AutoTBATS");
}

TEST_CASE("AutoTBATS builder default", "[auto_tbats][builder]") {
	auto auto_tbats = AutoTBATSBuilder().build();
	
	REQUIRE(auto_tbats->getName() == "AutoTBATS");
}

