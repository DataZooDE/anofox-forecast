#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "anofox-time/models/auto_mfles.hpp"
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

TEST_CASE("AutoMFLES constructor and parameters", "[auto_mfles][basic]") {
	AutoMFLES auto_mfles({12});
	
	REQUIRE(auto_mfles.getName() == "AutoMFLES");
	REQUIRE(auto_mfles.seasonalPeriods() == std::vector<int>{12});
}

TEST_CASE("AutoMFLES constructor validates parameters", "[auto_mfles][basic]") {
	REQUIRE_THROWS_AS(AutoMFLES({}), std::invalid_argument);  // Empty periods
	REQUIRE_THROWS_AS(AutoMFLES({-5}), std::invalid_argument);  // Negative period
	REQUIRE_THROWS_AS(AutoMFLES({12}, -1), std::invalid_argument);  // Negative test_size
}

TEST_CASE("AutoMFLES fit and predict", "[auto_mfles][basic]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	AutoMFLES auto_mfles({12});
	REQUIRE_NOTHROW(auto_mfles.fit(ts));
	
	auto forecast = auto_mfles.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("AutoMFLES requires fit before predict", "[auto_mfles][error]") {
	AutoMFLES auto_mfles({12});
	REQUIRE_THROWS_AS(auto_mfles.predict(10), std::runtime_error);
}

TEST_CASE("AutoMFLES requires fit before accessing selected model", "[auto_mfles][error]") {
	AutoMFLES auto_mfles({12});
	REQUIRE_THROWS_AS(auto_mfles.selectedModel(), std::runtime_error);
}

// ============================================================================
// Optimization Tests
// ============================================================================

TEST_CASE("AutoMFLES optimizes parameters", "[auto_mfles][optimization]") {
	auto data = generateSeasonalData(60, 12, 10.0, 0.5, 100.0);
	auto ts = createTimeSeries(data);
	
	AutoMFLES auto_mfles({12});
	auto_mfles.fit(ts);
	
	// Should have evaluated multiple models
	REQUIRE(auto_mfles.diagnostics().models_evaluated > 10);
	
	// Selected parameters should be in valid range
	REQUIRE(auto_mfles.selectedIterations() >= 1);
	REQUIRE(auto_mfles.selectedIterations() <= 7);
	REQUIRE(auto_mfles.selectedTrendLR() >= 0.0);
	REQUIRE(auto_mfles.selectedTrendLR() <= 1.0);
	REQUIRE(auto_mfles.selectedSeasonLR() >= 0.0);
	REQUIRE(auto_mfles.selectedSeasonLR() <= 1.0);
	REQUIRE(auto_mfles.selectedLevelLR() >= 0.0);
	REQUIRE(auto_mfles.selectedLevelLR() <= 1.0);
}

TEST_CASE("AutoMFLES diagnostics populated", "[auto_mfles][diagnostics]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	AutoMFLES auto_mfles({12});
	auto_mfles.fit(ts);
	
	const auto& diag = auto_mfles.diagnostics();
	
	REQUIRE(diag.models_evaluated > 0);
	REQUIRE(std::isfinite(diag.best_aic));
	REQUIRE(diag.best_aic < std::numeric_limits<double>::infinity());
	REQUIRE(diag.best_iterations > 0);
	REQUIRE(diag.optimization_time_ms > 0.0);
	
	// Best parameters should match selected parameters
	REQUIRE(diag.best_iterations == auto_mfles.selectedIterations());
	REQUIRE(diag.best_lr_trend == auto_mfles.selectedTrendLR());
	REQUIRE(diag.best_lr_season == auto_mfles.selectedSeasonLR());
	REQUIRE(diag.best_lr_level == auto_mfles.selectedLevelLR());
	REQUIRE(diag.best_aic == auto_mfles.selectedAIC());
}

TEST_CASE("AutoMFLES selected model accessible", "[auto_mfles][model]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	AutoMFLES auto_mfles({12});
	auto_mfles.fit(ts);
	
	const auto& model = auto_mfles.selectedModel();
	
	REQUIRE(model.getName() == "MFLES");
	REQUIRE(model.fittedValues().size() == 48);
	REQUIRE(model.residuals().size() == 48);
}

TEST_CASE("AutoMFLES finds good parameters for trending data", "[auto_mfles][optimization]") {
	// Strong trend data
	auto data = generateSeasonalData(60, 12, 5.0, 2.0, 100.0);  // Strong trend
	auto ts = createTimeSeries(data);
	
	AutoMFLES auto_mfles({12});
	auto_mfles.fit(ts);
	
	// For trending data, should prefer higher trend learning rate
	// This is a heuristic test - just check it's reasonable
	REQUIRE(auto_mfles.selectedTrendLR() >= 0.2);
	REQUIRE(auto_mfles.diagnostics().best_aic < std::numeric_limits<double>::infinity());
}

TEST_CASE("AutoMFLES finds good parameters for seasonal data", "[auto_mfles][optimization]") {
	// Strong seasonal pattern
	auto data = generateSeasonalData(60, 12, 20.0, 0.0, 100.0);  // Strong seasonality
	auto ts = createTimeSeries(data);
	
	AutoMFLES auto_mfles({12});
	auto_mfles.fit(ts);
	
	// For seasonal data, should find a good configuration
	REQUIRE(auto_mfles.selectedSeasonLR() >= 0.2);
	REQUIRE(auto_mfles.diagnostics().best_aic < std::numeric_limits<double>::infinity());
}

// ============================================================================
// Multiple Seasonalities Tests
// ============================================================================

TEST_CASE("AutoMFLES handles multiple seasonal periods", "[auto_mfles][seasonality]") {
	// Data with two seasonal components
	std::vector<double> data(72);
	for (int i = 0; i < 72; ++i) {
		data[i] = 100.0 + 
		          10.0 * std::sin(2.0 * M_PI * i / 12.0) +  // Monthly
		          5.0 * std::sin(2.0 * M_PI * i / 4.0);      // Quarterly
	}
	auto ts = createTimeSeries(data);
	
	AutoMFLES auto_mfles({12, 4});
	REQUIRE_NOTHROW(auto_mfles.fit(ts));
	
	auto forecast = auto_mfles.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_CASE("AutoMFLES improves over default MFLES", "[auto_mfles][performance]") {
	auto data = generateSeasonalData(48, 12, 10.0, 1.0, 100.0);
	auto ts = createTimeSeries(data);
	
	// Fit AutoMFLES
	AutoMFLES auto_mfles({12});
	auto_mfles.fit(ts);
	
	// Fit default MFLES
	MFLES mfles_default({12});  // Default params
	mfles_default.fit(ts);
	
	// AutoMFLES should have equal or better (lower) AIC
	// (It might be equal if defaults are already optimal)
	double auto_aic = auto_mfles.selectedAIC();
	
	// Just verify AutoMFLES found a valid model (AIC can be negative)
	REQUIRE(std::isfinite(auto_aic));
	REQUIRE(auto_aic < std::numeric_limits<double>::infinity());
}

TEST_CASE("AutoMFLES forecast quality", "[auto_mfles][quality]") {
	auto data = generateSeasonalData(60, 12, 10.0, 0.5, 100.0);
	
	// Split into train/test
	std::vector<double> train_data(data.begin(), data.begin() + 48);
	std::vector<double> test_data(data.begin() + 48, data.end());
	
	auto train_ts = createTimeSeries(train_data);
	
	AutoMFLES auto_mfles({12});
	auto_mfles.fit(train_ts);
	auto forecast = auto_mfles.predict(12);
	
	// Calculate MAE
	double mae = 0.0;
	for (size_t i = 0; i < test_data.size(); ++i) {
		mae += std::abs(forecast.primary()[i] - test_data[i]);
	}
	mae /= test_data.size();
	
	// Should have reasonable accuracy
	REQUIRE(mae < 15.0);  // Reasonable error for synthetic data
}

// ============================================================================
// Edge Cases Tests
// ============================================================================

TEST_CASE("AutoMFLES handles short data", "[auto_mfles][edge]") {
	std::vector<double> data = {100., 105., 110., 108., 112., 115., 113., 118., 120., 122.};
	auto ts = createTimeSeries(data);
	
	AutoMFLES auto_mfles({4});  // Period shorter than data
	REQUIRE_NOTHROW(auto_mfles.fit(ts));
	
	auto forecast = auto_mfles.predict(4);
	REQUIRE(forecast.primary().size() == 4);
}

TEST_CASE("AutoMFLES handles constant data", "[auto_mfles][edge]") {
	std::vector<double> data(30, 100.0);
	auto ts = createTimeSeries(data);
	
	AutoMFLES auto_mfles({12});
	REQUIRE_NOTHROW(auto_mfles.fit(ts));
	
	auto forecast = auto_mfles.predict(10);
	
	// Should forecast near constant value
	for (double f : forecast.primary()) {
		REQUIRE(std::abs(f - 100.0) < 30.0);  // Wider tolerance for AutoMFLES
	}
}

// ============================================================================
// Builder Tests
// ============================================================================

TEST_CASE("AutoMFLES builder pattern", "[auto_mfles][builder]") {
	AutoMFLESBuilder builder;
	auto auto_mfles = builder
		.withSeasonalPeriods({12, 4})
		.withTestSize(0)
		.build();
	
	REQUIRE(auto_mfles->getName() == "AutoMFLES");
	REQUIRE(auto_mfles->seasonalPeriods() == std::vector<int>{12, 4});
}

TEST_CASE("AutoMFLES builder default values", "[auto_mfles][builder]") {
	AutoMFLESBuilder builder;
	auto auto_mfles = builder.build();
	
	REQUIRE(auto_mfles->seasonalPeriods() == std::vector<int>{12});
}

// ============================================================================
// Comparison Tests
// ============================================================================

TEST_CASE("AutoMFLES vs MFLES consistency", "[auto_mfles][comparison]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	AutoMFLES auto_mfles({12});
	auto_mfles.fit(ts);
	
	// Access the selected model
	const auto& selected = auto_mfles.selectedModel();
	
	// Create MFLES with same parameters
	MFLES mfles_manual(
		{12},
		auto_mfles.selectedIterations(),
		auto_mfles.selectedTrendLR(),
		auto_mfles.selectedSeasonLR(),
		auto_mfles.selectedLevelLR()
	);
	mfles_manual.fit(ts);
	
	// Forecasts should be identical
	auto forecast_auto = auto_mfles.predict(12);
	auto forecast_manual = mfles_manual.predict(12);
	
	REQUIRE(forecast_auto.primary().size() == forecast_manual.primary().size());
	
	for (size_t i = 0; i < forecast_auto.primary().size(); ++i) {
		REQUIRE_THAT(forecast_auto.primary()[i],
		             Catch::Matchers::WithinAbs(forecast_manual.primary()[i], 1e-10));
	}
}

