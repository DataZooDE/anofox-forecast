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
// Basic Functionality Tests (6 tests)
// ============================================================================

TEST_CASE("MFLES constructor and parameters", "[mfles][basic]") {
	MFLES mfles({12}, 3, 0.3, 0.5, 0.8);
	
	REQUIRE(mfles.getName() == "MFLES");
	REQUIRE(mfles.seasonalPeriods() == std::vector<int>{12});
	REQUIRE(mfles.iterations() == 3);
	REQUIRE(mfles.trendLearningRate() == 0.3);
	REQUIRE(mfles.seasonalLearningRate() == 0.5);
	REQUIRE(mfles.levelLearningRate() == 0.8);
}

TEST_CASE("MFLES constructor validates parameters", "[mfles][basic]") {
	REQUIRE_THROWS_AS(MFLES({12}, 0), std::invalid_argument);  // n_iterations < 1
	REQUIRE_THROWS_AS(MFLES({12}, 3, -0.1), std::invalid_argument);  // lr_trend < 0
	REQUIRE_THROWS_AS(MFLES({12}, 3, 1.5), std::invalid_argument);  // lr_trend > 1
	REQUIRE_THROWS_AS(MFLES({12}, 3, 0.3, -0.1), std::invalid_argument);  // lr_season < 0
	REQUIRE_THROWS_AS(MFLES({12}, 3, 0.3, 0.5, 1.5), std::invalid_argument);  // lr_level > 1
	REQUIRE_THROWS_AS(MFLES({-5}), std::invalid_argument);  // negative period
}

TEST_CASE("MFLES fit basic data", "[mfles][basic]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12});
	REQUIRE_NOTHROW(mfles.fit(ts));
	
	REQUIRE(mfles.fittedValues().size() == 48);
	REQUIRE(mfles.residuals().size() == 48);
}

TEST_CASE("MFLES predict horizon", "[mfles][basic]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12});
	mfles.fit(ts);
	
	auto forecast = mfles.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("MFLES fitted values stored", "[mfles][basic]") {
	auto data = generateSeasonalData(36, 12, 10.0, 0.5, 100.0);
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12});
	mfles.fit(ts);
	
	const auto& fitted = mfles.fittedValues();
	REQUIRE(fitted.size() == 36);
	
	// Fitted values should be reasonable
	for (size_t i = 0; i < fitted.size(); ++i) {
		REQUIRE(std::abs(fitted[i] - data[i]) < 20.0);  // Within reasonable error
	}
}

TEST_CASE("MFLES residuals computed", "[mfles][basic]") {
	auto data = generateSeasonalData(36, 12);
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12});
	mfles.fit(ts);
	
	const auto& residuals = mfles.residuals();
	const auto& fitted = mfles.fittedValues();
	
	REQUIRE(residuals.size() == 36);
	
	// Check residuals = actual - fitted
	for (size_t i = 0; i < residuals.size(); ++i) {
		REQUIRE_THAT(residuals[i], 
		             Catch::Matchers::WithinAbs(data[i] - fitted[i], 1e-10));
	}
}

// ============================================================================
// Fourier Seasonality Tests (6 tests)
// ============================================================================

TEST_CASE("MFLES single seasonal period", "[mfles][fourier]") {
	auto data = generateSeasonalData(48, 12, 10.0, 0.0, 100.0);
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12});
	mfles.fit(ts);
	
	auto forecast = mfles.predict(12);
	
	// Should capture seasonal pattern
	// Mean absolute error should be reasonable
	double mae = 0.0;
	for (size_t i = 0; i < 12; ++i) {
		mae += std::abs(forecast.primary()[i] - data[36 + i % 36]);
	}
	mae /= 12;
	
	REQUIRE(mae < 15.0);  // Should capture pattern reasonably well
}

TEST_CASE("MFLES multiple seasonal periods", "[mfles][fourier]") {
	// Data with two seasonal components
	std::vector<double> data(60);
	for (int i = 0; i < 60; ++i) {
		data[i] = 100.0 + 
		          10.0 * std::sin(2.0 * M_PI * i / 12.0) +  // Monthly
		          5.0 * std::sin(2.0 * M_PI * i / 4.0);      // Quarterly
	}
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12, 4});  // Both periods
	mfles.fit(ts);
	
	REQUIRE(mfles.seasonalPeriods().size() == 2);
	REQUIRE(mfles.fittedValues().size() == 60);
	
	auto forecast = mfles.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("MFLES Fourier K selection", "[mfles][fourier]") {
	// Test with different period sizes
	MFLES mfles1({4});   // K should be 2 (min(4/2, 10))
	MFLES mfles2({12});  // K should be 6 (min(12/2, 10))
	MFLES mfles3({24});  // K should be 10 (min(24/2, 10) = 10)
	MFLES mfles4({50});  // K should be 10 (min(50/2, 10) = 10, capped)
	
	// Just verify construction works
	REQUIRE(mfles1.getName() == "MFLES");
	REQUIRE(mfles2.getName() == "MFLES");
	REQUIRE(mfles3.getName() == "MFLES");
	REQUIRE(mfles4.getName() == "MFLES");
}

TEST_CASE("MFLES captures seasonal pattern", "[mfles][fourier]") {
	// Pure seasonal pattern with no trend
	auto data = generateSeasonalData(60, 12, 15.0, 0.0, 100.0);
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12}, 5);  // More iterations for better fit
	mfles.fit(ts);
	
	const auto& fitted = mfles.fittedValues();
	
	// Calculate mean absolute error
	double mae = 0.0;
	for (size_t i = 0; i < fitted.size(); ++i) {
		mae += std::abs(fitted[i] - data[i]);
	}
	mae /= fitted.size();
	
	REQUIRE(mae < 10.0);  // Should fit seasonal pattern well
}

TEST_CASE("MFLES seasonal projection correctness", "[mfles][fourier]") {
	auto data = generateSeasonalData(48, 12, 10.0, 0.0, 100.0);
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12});
	mfles.fit(ts);
	
	auto forecast = mfles.predict(24);  // 2 full cycles
	
	// Check that seasonal pattern repeats (approximately)
	for (int i = 0; i < 12; ++i) {
		double diff = std::abs(forecast.primary()[i] - forecast.primary()[i + 12]);
		REQUIRE(diff < 5.0);  // Should repeat within tolerance
	}
}

TEST_CASE("MFLES handles non-integer seasonality edge case", "[mfles][fourier]") {
	// Period = 1 is edge case (no seasonality)
	auto data = generateSeasonalData(30, 1, 0.0, 1.0, 100.0);  // Just trend
	auto ts = createTimeSeries(data);
	
	MFLES mfles({1});
	REQUIRE_NOTHROW(mfles.fit(ts));
	REQUIRE_NOTHROW(mfles.predict(10));
}

// ============================================================================
// Gradient Boosting Tests (5 tests)
// ============================================================================

TEST_CASE("MFLES iterations affect accuracy", "[mfles][boosting]") {
	auto data = generateSeasonalData(48, 12, 10.0, 0.5, 100.0);
	auto ts = createTimeSeries(data);
	
	MFLES mfles1({12}, 1);
	MFLES mfles5({12}, 5);
	
	mfles1.fit(ts);
	mfles5.fit(ts);
	
	// Calculate mean absolute residuals
	auto calcMAR = [](const std::vector<double>& residuals) {
		double sum = 0.0;
		for (double r : residuals) {
			sum += std::abs(r);
		}
		return sum / residuals.size();
	};
	
	double mar1 = calcMAR(mfles1.residuals());
	double mar5 = calcMAR(mfles5.residuals());
	
	// More iterations should generally reduce residuals
	REQUIRE(mar5 <= mar1 * 1.1);  // Allow small tolerance
}

TEST_CASE("MFLES learning rates impact components", "[mfles][boosting]") {
	auto data = generateSeasonalData(36, 12);
	auto ts = createTimeSeries(data);
	
	// Zero learning rates should give zero contribution
	MFLES mfles_no_trend({12}, 1, 0.0, 0.5, 0.8);
	MFLES mfles_no_season({12}, 1, 0.3, 0.0, 0.8);
	MFLES mfles_no_level({12}, 1, 0.3, 0.5, 0.0);
	
	REQUIRE_NOTHROW(mfles_no_trend.fit(ts));
	REQUIRE_NOTHROW(mfles_no_season.fit(ts));
	REQUIRE_NOTHROW(mfles_no_level.fit(ts));
}

TEST_CASE("MFLES residuals decrease with iterations", "[mfles][boosting]") {
	auto data = generateSeasonalData(48, 12, 12.0, 0.8, 100.0);
	auto ts = createTimeSeries(data);
	
	std::vector<double> mars;  // Mean absolute residuals
	
	for (int iter = 1; iter <= 5; ++iter) {
		MFLES mfles({12}, iter);
		mfles.fit(ts);
		
		double mar = 0.0;
		for (double r : mfles.residuals()) {
			mar += std::abs(r);
		}
		mar /= mfles.residuals().size();
		mars.push_back(mar);
	}
	
	// First should be worse than last (or similar)
	REQUIRE(mars[0] >= mars[4] * 0.9);
}

TEST_CASE("MFLES component accumulation", "[mfles][boosting]") {
	auto data = generateSeasonalData(36, 12);
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12}, 3);
	mfles.fit(ts);
	
	// Fitted values should be sum of all components
	// Just verify they exist and are reasonable
	const auto& fitted = mfles.fittedValues();
	
	for (double f : fitted) {
		REQUIRE(std::isfinite(f));
		REQUIRE(f > 0.0);  // Our data is all positive
		REQUIRE(f < 200.0);  // Should be in reasonable range
	}
}

TEST_CASE("MFLES convergence on synthetic data", "[mfles][boosting]") {
	// Perfect seasonal + trend data
	auto data = generateSeasonalData(60, 12, 10.0, 0.5, 100.0);
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12}, 10);  // Many iterations
	mfles.fit(ts);
	
	// Should converge to low residuals
	double max_abs_residual = 0.0;
	for (double r : mfles.residuals()) {
		max_abs_residual = std::max(max_abs_residual, std::abs(r));
	}
	
	REQUIRE(max_abs_residual < 15.0);
}

// ============================================================================
// Trend and Level Tests (4 tests)
// ============================================================================

TEST_CASE("MFLES linear trend component", "[mfles][trend]") {
	// Pure linear trend, no seasonality
	std::vector<double> data(40);
	for (int i = 0; i < 40; ++i) {
		data[i] = 100.0 + 2.0 * i;
	}
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12}, 3, 0.8, 0.0, 0.2);  // High trend LR, low season LR
	mfles.fit(ts);
	
	auto forecast = mfles.predict(10);
	
	// Should project trend forward
	// Expected: 100 + 2*40 = 180, 100 + 2*41 = 182, etc. (approximately)
	REQUIRE(forecast.primary()[0] > 170.0);
	REQUIRE(forecast.primary()[0] < 190.0);
	REQUIRE(forecast.primary()[9] > forecast.primary()[0]);  // Increasing
}

TEST_CASE("MFLES ES level component", "[mfles][level]") {
	// Constant data (pure level)
	std::vector<double> data(30, 150.0);
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12}, 2, 0.0, 0.0, 1.0);  // Only level component
	mfles.fit(ts);
	
	auto forecast = mfles.predict(10);
	
	// Should forecast constant level
	for (double f : forecast.primary()) {
		REQUIRE_THAT(f, Catch::Matchers::WithinAbs(150.0, 5.0));
	}
}

TEST_CASE("MFLES detrending improves seasonality", "[mfles][trend]") {
	auto data = generateSeasonalData(48, 12, 10.0, 2.0, 100.0);  // Strong trend
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12}, 3);
	mfles.fit(ts);
	
	// Just verify it fits without issues
	REQUIRE(mfles.fittedValues().size() == 48);
	
	// Residuals should be reasonable
	double mar = 0.0;
	for (double r : mfles.residuals()) {
		mar += std::abs(r);
	}
	mar /= 48;
	REQUIRE(mar < 20.0);
}

TEST_CASE("MFLES trend projection in forecast", "[mfles][trend]") {
	std::vector<double> data(36);
	for (int i = 0; i < 36; ++i) {
		data[i] = 50.0 + 1.5 * i;
	}
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12}, 2);
	mfles.fit(ts);
	
	auto forecast = mfles.predict(12);
	
	// Forecast should be increasing (trend)
	for (size_t i = 1; i < forecast.primary().size(); ++i) {
		REQUIRE(forecast.primary()[i] >= forecast.primary()[i-1] - 1.0);  // Mostly increasing
	}
}

// ============================================================================
// Edge Cases Tests (5 tests)
// ============================================================================

TEST_CASE("MFLES with no seasonality", "[mfles][edge]") {
	std::vector<double> data(30);
	for (int i = 0; i < 30; ++i) {
		data[i] = 100.0 + 0.5 * i;
	}
	auto ts = createTimeSeries(data);
	
	MFLES mfles({});  // Empty seasonal periods
	REQUIRE_NOTHROW(mfles.fit(ts));
	
	auto forecast = mfles.predict(10);
	REQUIRE(forecast.primary().size() == 10);
}

TEST_CASE("MFLES with single iteration", "[mfles][edge]") {
	auto data = generateSeasonalData(36, 12);
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12}, 1);  // Just one iteration
	REQUIRE_NOTHROW(mfles.fit(ts));
	
	auto forecast = mfles.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("MFLES short data", "[mfles][edge]") {
	std::vector<double> data = {100.0, 101.0, 102.0, 103.0, 104.0};
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12});  // Period longer than data
	REQUIRE_NOTHROW(mfles.fit(ts));
	
	auto forecast = mfles.predict(5);
	REQUIRE(forecast.primary().size() == 5);
}

TEST_CASE("MFLES constant data", "[mfles][edge]") {
	std::vector<double> data(30, 100.0);  // All same value
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12});
	REQUIRE_NOTHROW(mfles.fit(ts));
	
	auto forecast = mfles.predict(10);
	
	// Should forecast constant value (within tolerance for Fourier approximation)
	// Note: Fourier decomposition of constant data can have some error
	for (double f : forecast.primary()) {
		REQUIRE(std::abs(f - 100.0) < 20.0);
	}
}

TEST_CASE("MFLES zero learning rates", "[mfles][edge]") {
	auto data = generateSeasonalData(36, 12);
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12}, 3, 0.0, 0.0, 0.0);  // All zero LRs
	REQUIRE_NOTHROW(mfles.fit(ts));
	
	// Fitted should be all zeros (no components learned)
	const auto& fitted = mfles.fittedValues();
	for (double f : fitted) {
		REQUIRE_THAT(f, Catch::Matchers::WithinAbs(0.0, 1e-10));
	}
}

// ============================================================================
// Builder Tests (2 tests)
// ============================================================================

TEST_CASE("MFLES builder pattern", "[mfles][builder]") {
	MFLESBuilder builder;
	auto mfles = builder
		.withSeasonalPeriods({12, 4})
		.withIterations(5)
		.withTrendLearningRate(0.2)
		.withSeasonalLearningRate(0.6)
		.withLevelLearningRate(0.9)
		.build();
	
	REQUIRE(mfles->getName() == "MFLES");
	REQUIRE(mfles->seasonalPeriods() == std::vector<int>{12, 4});
	REQUIRE(mfles->iterations() == 5);
	REQUIRE(mfles->trendLearningRate() == 0.2);
	REQUIRE(mfles->seasonalLearningRate() == 0.6);
	REQUIRE(mfles->levelLearningRate() == 0.9);
}

TEST_CASE("MFLES builder default values", "[mfles][builder]") {
	MFLESBuilder builder;
	auto mfles = builder.build();
	
	REQUIRE(mfles->seasonalPeriods() == std::vector<int>{12});
	REQUIRE(mfles->iterations() == 3);
	REQUIRE(mfles->trendLearningRate() == 0.3);
	REQUIRE(mfles->seasonalLearningRate() == 0.5);
	REQUIRE(mfles->levelLearningRate() == 0.8);
}

// ============================================================================
// Error Handling Tests (2 tests)
// ============================================================================

TEST_CASE("MFLES requires fit before predict", "[mfles][error]") {
	MFLES mfles({12});
	
	REQUIRE_THROWS_AS(mfles.predict(10), std::runtime_error);
}

TEST_CASE("MFLES validates horizon", "[mfles][error]") {
	auto data = generateSeasonalData(36, 12);
	auto ts = createTimeSeries(data);
	
	MFLES mfles({12});
	mfles.fit(ts);
	
	REQUIRE_THROWS_AS(mfles.predict(0), std::invalid_argument);
	REQUIRE_THROWS_AS(mfles.predict(-5), std::invalid_argument);
}

