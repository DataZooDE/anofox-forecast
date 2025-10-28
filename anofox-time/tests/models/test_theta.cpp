#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/models/theta.hpp"
#include "anofox-time/models/optimized_theta.hpp"
#include "anofox-time/models/dynamic_theta.hpp"
#include "anofox-time/models/dynamic_optimized_theta.hpp"
#include "common/time_series_helpers.hpp"

#include <cmath>
#include <vector>

using anofoxtime::models::Theta;
using anofoxtime::models::OptimizedTheta;
using anofoxtime::models::DynamicTheta;
using anofoxtime::models::DynamicOptimizedTheta;

namespace {

// Generate trending data
std::vector<double> generateTrendingData(std::size_t n, double slope = 0.5, double intercept = 10.0) {
	std::vector<double> data(n);
	for (std::size_t i = 0; i < n; ++i) {
		data[i] = intercept + slope * static_cast<double>(i);
	}
	return data;
}

// Generate seasonal data
std::vector<double> generateSeasonalData(std::size_t cycles, int period = 12) {
	std::vector<double> data;
	data.reserve(cycles * static_cast<std::size_t>(period));
	
	for (std::size_t c = 0; c < cycles; ++c) {
		for (int t = 0; t < period; ++t) {
			const double seasonal = 10.0 * std::sin(2.0 * M_PI * static_cast<double>(t) / static_cast<double>(period));
			const double trend = 100.0 + 0.1 * static_cast<double>(c * period + t);
			data.push_back(trend + seasonal);
		}
	}
	return data;
}

} // namespace

// ==========================
// Basic Theta Tests
// ==========================

TEST_CASE("Theta constructor accepts valid parameters", "[models][theta]") {
	REQUIRE_NOTHROW(Theta(1, 2.0));
	REQUIRE_NOTHROW(Theta(12, 2.0));
	REQUIRE_NOTHROW(Theta(4, 1.5));
	
	REQUIRE_THROWS_AS(Theta(0, 2.0), std::invalid_argument);
	REQUIRE_THROWS_AS(Theta(-1, 2.0), std::invalid_argument);
	REQUIRE_THROWS_AS(Theta(12, -0.1), std::invalid_argument);
}

TEST_CASE("Theta basic forecast on non-seasonal data", "[models][theta]") {
	const std::vector<double> data = generateTrendingData(20, 0.5, 10.0);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	Theta model(1, 2.0);  // Non-seasonal, theta=2
	model.fit(ts);
	
	auto forecast = model.predict(3);
	REQUIRE(forecast.primary().size() == 3);
	
	// Should trend upward (allow small numerical differences)
	REQUIRE(forecast.primary()[0] >= data.back() - 0.5);
	REQUIRE(forecast.primary()[1] >= forecast.primary()[0] - 0.5);
}

TEST_CASE("Theta with seasonal data", "[models][theta][seasonal]") {
	const auto data = generateSeasonalData(10, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	Theta model(12, 2.0);  // Monthly seasonality
	model.fit(ts);
	
	auto forecast = model.predict(12);
	REQUIRE(forecast.primary().size() == 12);
	
	// All forecasts should be valid
	for (double val : forecast.primary()) {
		REQUIRE(std::isfinite(val));
	}
}

TEST_CASE("Theta different theta parameters", "[models][theta][params]") {
	const auto data = generateTrendingData(30);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// Theta = 0 (linear trend only)
	Theta model0(1, 0.0);
	model0.fit(ts);
	auto forecast0 = model0.predict(5);
	
	// Theta = 2 (standard)
	Theta model2(1, 2.0);
	model2.fit(ts);
	auto forecast2 = model2.predict(5);
	
	// Theta = 3 (enhanced curvature)
	Theta model3(1, 3.0);
	model3.fit(ts);
	auto forecast3 = model3.predict(5);
	
	// All should produce valid forecasts
	REQUIRE(forecast0.primary().size() == 5);
	REQUIRE(forecast2.primary().size() == 5);
	REQUIRE(forecast3.primary().size() == 5);
}

TEST_CASE("Theta fitted values and residuals", "[models][theta]") {
	const auto data = generateTrendingData(20);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	Theta model(1, 2.0);
	model.fit(ts);
	
	const auto& fitted = model.fittedValues();
	const auto& residuals = model.residuals();
	
	REQUIRE(fitted.size() == data.size());
	REQUIRE(residuals.size() == data.size());
	
	// Residuals should have reasonable mean
	double sum_res = std::accumulate(residuals.begin(), residuals.end(), 0.0);
	double mean_res = sum_res / static_cast<double>(residuals.size());
	REQUIRE(std::abs(mean_res) < 10.0);
}

TEST_CASE("Theta confidence intervals", "[models][theta]") {
	const auto data = generateTrendingData(30);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	Theta model(1, 2.0);
	model.fit(ts);
	
	const int horizon = 5;
	auto forecast = model.predictWithConfidence(horizon, 0.95);
	
	REQUIRE(forecast.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(forecast.lowerSeries().size() == static_cast<std::size_t>(horizon));
	REQUIRE(forecast.upperSeries().size() == static_cast<std::size_t>(horizon));
	
	// Lower <= Primary <= Upper
	for (std::size_t i = 0; i < static_cast<std::size_t>(horizon); ++i) {
		REQUIRE(forecast.lowerSeries()[i] <= forecast.primary()[i]);
		REQUIRE(forecast.upperSeries()[i] >= forecast.primary()[i]);
	}
}

// ==========================
// OptimizedTheta Tests
// ==========================

TEST_CASE("OptimizedTheta constructor", "[models][theta][optimized]") {
	REQUIRE_NOTHROW(OptimizedTheta(1));
	REQUIRE_NOTHROW(OptimizedTheta(12));
	REQUIRE_THROWS_AS(OptimizedTheta(0), std::invalid_argument);
}

TEST_CASE("OptimizedTheta finds optimal parameters", "[models][theta][optimized]") {
	const auto data = generateTrendingData(40, 0.3, 50.0);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	OptimizedTheta model(1);
	model.fit(ts);
	
	// Should have found optimal theta and alpha
	REQUIRE(model.optimalTheta() >= 1.0);
	REQUIRE(model.optimalTheta() <= 3.0);
	REQUIRE(model.optimalAlpha() >= 0.05);
	REQUIRE(model.optimalAlpha() <= 0.95);
	REQUIRE(std::isfinite(model.optimalAIC()));
	
	// Should produce forecasts
	auto forecast = model.predict(5);
	REQUIRE(forecast.primary().size() == 5);
}

TEST_CASE("OptimizedTheta parameter ranges", "[models][theta][optimized]") {
	OptimizedTheta model(1);
	
	// Set custom ranges
	REQUIRE_NOTHROW(model.setThetaRange(1.5, 2.5));
	REQUIRE_NOTHROW(model.setAlphaRange(0.1, 0.9));
	REQUIRE_NOTHROW(model.setThetaStep(0.2));
	REQUIRE_NOTHROW(model.setAlphaStep(0.1));
	
	// Invalid ranges
	REQUIRE_THROWS_AS(model.setThetaRange(2.5, 1.5), std::invalid_argument);
	REQUIRE_THROWS_AS(model.setAlphaRange(0.9, 0.1), std::invalid_argument);
	REQUIRE_THROWS_AS(model.setThetaStep(-0.1), std::invalid_argument);
}

TEST_CASE("OptimizedTheta on seasonal data", "[models][theta][optimized][seasonal]") {
	const auto data = generateSeasonalData(12, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	OptimizedTheta model(12);
	model.fit(ts);
	
	auto forecast = model.predict(12);
	REQUIRE(forecast.primary().size() == 12);
	
	// Check fitted values
	const auto& fitted = model.fittedValues();
	REQUIRE(!fitted.empty());
}

TEST_CASE("OptimizedTheta confidence intervals", "[models][theta][optimized]") {
	const auto data = generateTrendingData(50);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	OptimizedTheta model(1);
	model.fit(ts);
	
	auto forecast = model.predictWithConfidence(10, 0.95);
	REQUIRE(forecast.primary().size() == 10);
	REQUIRE(forecast.lowerSeries().size() == 10);
	REQUIRE(forecast.upperSeries().size() == 10);
}

// ==========================
// DynamicTheta Tests
// ==========================

TEST_CASE("DynamicTheta constructor", "[models][theta][dynamic]") {
	REQUIRE_NOTHROW(DynamicTheta(1));
	REQUIRE_NOTHROW(DynamicTheta(12));
	REQUIRE_THROWS_AS(DynamicTheta(0), std::invalid_argument);
}

TEST_CASE("DynamicTheta basic forecast", "[models][theta][dynamic]") {
	const auto data = generateTrendingData(40, 0.5, 100.0);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	DynamicTheta model(1);
	model.fit(ts);
	
	// Should have optimized parameters
	REQUIRE(model.alphaLevel() >= 0.05);
	REQUIRE(model.alphaLevel() <= 0.95);
	REQUIRE(model.betaTrend() >= 0.01);
	REQUIRE(model.betaTrend() <= 0.50);
	
	auto forecast = model.predict(5);
	REQUIRE(forecast.primary().size() == 5);
	
	// Should be trending
	REQUIRE(forecast.primary()[1] > forecast.primary()[0]);
}

TEST_CASE("DynamicTheta manual parameters", "[models][theta][dynamic]") {
	DynamicTheta model(1);
	
	REQUIRE_NOTHROW(model.setAlphaLevel(0.7));
	REQUIRE_NOTHROW(model.setBetaTrend(0.3));
	
	REQUIRE(model.alphaLevel() == Catch::Approx(0.7));
	REQUIRE(model.betaTrend() == Catch::Approx(0.3));
	
	REQUIRE_THROWS_AS(model.setAlphaLevel(1.5), std::invalid_argument);
	REQUIRE_THROWS_AS(model.setBetaTrend(-0.1), std::invalid_argument);
}

TEST_CASE("DynamicTheta on seasonal data", "[models][theta][dynamic][seasonal]") {
	const auto data = generateSeasonalData(10, 4);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	DynamicTheta model(4);  // Quarterly
	model.fit(ts);
	
	auto forecast = model.predict(8);
	REQUIRE(forecast.primary().size() == 8);
	
	for (double val : forecast.primary()) {
		REQUIRE(std::isfinite(val));
	}
}

TEST_CASE("DynamicTheta confidence intervals", "[models][theta][dynamic]") {
	const auto data = generateTrendingData(50);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	DynamicTheta model(1);
	model.fit(ts);
	
	auto forecast = model.predictWithConfidence(10, 0.95);
	REQUIRE(forecast.lowerSeries().size() == 10);
	REQUIRE(forecast.upperSeries().size() == 10);
	
	for (std::size_t i = 0; i < 10; ++i) {
		REQUIRE(forecast.lowerSeries()[i] <= forecast.primary()[i]);
		REQUIRE(forecast.upperSeries()[i] >= forecast.primary()[i]);
	}
}

// ==========================
// DynamicOptimizedTheta Tests
// ==========================

TEST_CASE("DynamicOptimizedTheta constructor", "[models][theta][dynamic][optimized]") {
	REQUIRE_NOTHROW(DynamicOptimizedTheta(1));
	REQUIRE_NOTHROW(DynamicOptimizedTheta(12));
	REQUIRE_THROWS_AS(DynamicOptimizedTheta(0), std::invalid_argument);
}

TEST_CASE("DynamicOptimizedTheta finds optimal parameters", "[models][theta][dynamic][optimized]") {
	const auto data = generateTrendingData(60, 0.4, 80.0);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	DynamicOptimizedTheta model(1);
	model.fit(ts);
	
	// Should have found optimal parameters
	REQUIRE(model.optimalAlpha() >= 0.05);
	REQUIRE(model.optimalAlpha() <= 0.95);
	REQUIRE(model.optimalBeta() >= 0.01);
	REQUIRE(model.optimalBeta() <= 0.50);
	REQUIRE(std::isfinite(model.optimalAIC()));
	
	auto forecast = model.predict(10);
	REQUIRE(forecast.primary().size() == 10);
}

TEST_CASE("DynamicOptimizedTheta on seasonal data", "[models][theta][dynamic][optimized][seasonal]") {
	const auto data = generateSeasonalData(15, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	DynamicOptimizedTheta model(12);
	model.fit(ts);
	
	auto forecast = model.predict(12);
	REQUIRE(forecast.primary().size() == 12);
	
	// Check fitted values
	const auto& fitted = model.fittedValues();
	REQUIRE(!fitted.empty());
	
	// Check residuals
	const auto& residuals = model.residuals();
	REQUIRE(residuals.size() == data.size());
}

TEST_CASE("DynamicOptimizedTheta confidence intervals", "[models][theta][dynamic][optimized]") {
	const auto data = generateTrendingData(50);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	DynamicOptimizedTheta model(1);
	model.fit(ts);
	
	auto forecast = model.predictWithConfidence(10, 0.95);
	REQUIRE(forecast.primary().size() == 10);
	REQUIRE(forecast.lowerSeries().size() == 10);
	REQUIRE(forecast.upperSeries().size() == 10);
}

// ==========================
// Integration Tests
// ==========================

TEST_CASE("All Theta methods on same data", "[models][theta][integration]") {
	const auto data = generateTrendingData(50, 0.3, 100.0);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	Theta theta(1, 2.0);
	OptimizedTheta opt_theta(1);
	DynamicTheta dyn_theta(1);
	DynamicOptimizedTheta dyn_opt_theta(1);
	
	REQUIRE_NOTHROW(theta.fit(ts));
	REQUIRE_NOTHROW(opt_theta.fit(ts));
	REQUIRE_NOTHROW(dyn_theta.fit(ts));
	REQUIRE_NOTHROW(dyn_opt_theta.fit(ts));
	
	const int horizon = 10;
	auto f1 = theta.predict(horizon);
	auto f2 = opt_theta.predict(horizon);
	auto f3 = dyn_theta.predict(horizon);
	auto f4 = dyn_opt_theta.predict(horizon);
	
	REQUIRE(f1.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(f2.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(f3.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(f4.primary().size() == static_cast<std::size_t>(horizon));
	
	// All forecasts should be in reasonable range
	for (std::size_t i = 0; i < static_cast<std::size_t>(horizon); ++i) {
		REQUIRE(std::isfinite(f1.primary()[i]));
		REQUIRE(std::isfinite(f2.primary()[i]));
		REQUIRE(std::isfinite(f3.primary()[i]));
		REQUIRE(std::isfinite(f4.primary()[i]));
	}
}

TEST_CASE("Theta methods handle short series", "[models][theta][edge]") {
	const std::vector<double> short_data = {10.0, 11.0, 12.0, 13.0, 14.0};
	auto ts = tests::helpers::makeUnivariateSeries(short_data);
	
	Theta model(1, 2.0);
	REQUIRE_NOTHROW(model.fit(ts));
	
	auto forecast = model.predict(3);
	REQUIRE(forecast.primary().size() == 3);
}

TEST_CASE("Theta methods handle constant series", "[models][theta][edge]") {
	const std::vector<double> constant_data(20, 42.0);
	auto ts = tests::helpers::makeUnivariateSeries(constant_data);
	
	Theta model(1, 2.0);
	model.fit(ts);
	
	auto forecast = model.predict(5);
	REQUIRE(forecast.primary().size() == 5);
	
	// Forecast should be approximately constant
	for (double val : forecast.primary()) {
		REQUIRE(val == Catch::Approx(42.0).margin(5.0));
	}
}

TEST_CASE("Theta getName returns correct identifier", "[models][theta][metadata]") {
	Theta theta(1, 2.0);
	OptimizedTheta opt_theta(1);
	DynamicTheta dyn_theta(1);
	DynamicOptimizedTheta dyn_opt_theta(1);
	
	REQUIRE(theta.getName() == "Theta");
	REQUIRE(opt_theta.getName() == "OptimizedTheta");
	REQUIRE(dyn_theta.getName() == "DynamicTheta");
	REQUIRE(dyn_opt_theta.getName() == "DynamicOptimizedTheta");
}

TEST_CASE("Theta invalid inputs", "[models][theta][error]") {
	Theta model(1, 2.0);
	
	// Empty data
	std::vector<double> empty;
	auto ts_empty = tests::helpers::makeUnivariateSeries(empty);
	REQUIRE_THROWS_AS(model.fit(ts_empty), std::invalid_argument);
	
	// Predict before fit
	Theta unfitted(1, 2.0);
	REQUIRE_THROWS_AS(unfitted.predict(5), std::runtime_error);
}


