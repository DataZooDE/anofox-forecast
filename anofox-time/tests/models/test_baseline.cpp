#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/models/naive.hpp"
#include "anofox-time/models/random_walk_drift.hpp"
#include "anofox-time/models/seasonal_naive.hpp"
#include "anofox-time/models/seasonal_window_average.hpp"
#include "anofox-time/models/sma.hpp"
#include "common/time_series_helpers.hpp"

#include <cmath>
#include <vector>

using anofoxtime::models::Naive;
using anofoxtime::models::RandomWalkWithDrift;
using anofoxtime::models::SeasonalNaive;
using anofoxtime::models::SeasonalWindowAverage;
using anofoxtime::models::SimpleMovingAverageBuilder;

namespace {

std::vector<double> generateTrendingData(std::size_t n, double slope = 0.5) {
	std::vector<double> data(n);
	for (std::size_t i = 0; i < n; ++i) {
		data[i] = 100.0 + slope * static_cast<double>(i);
	}
	return data;
}

std::vector<double> generateSeasonalData(std::size_t cycles, int period = 12) {
	std::vector<double> data;
	for (std::size_t c = 0; c < cycles; ++c) {
		for (int t = 0; t < period; ++t) {
			data.push_back(100.0 + static_cast<double>(t) * 5.0);
		}
	}
	return data;
}

} // namespace

// ==========================
// Naive Tests
// ==========================

TEST_CASE("Naive repeats last value", "[models][baseline][naive]") {
	const std::vector<double> data = {10.0, 12.0, 15.0, 14.0, 16.0};
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	Naive model;
	model.fit(ts);
	auto forecast = model.predict(3);
	
	REQUIRE(forecast.primary().size() == 3);
	REQUIRE(forecast.primary()[0] == 16.0);  // Last value
	REQUIRE(forecast.primary()[1] == 16.0);
	REQUIRE(forecast.primary()[2] == 16.0);
}

TEST_CASE("Naive fitted values are shifted history", "[models][baseline][naive]") {
	const std::vector<double> data = {10.0, 12.0, 15.0, 14.0, 16.0};
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	Naive model;
	model.fit(ts);
	
	const auto& fitted = model.fittedValues();
	REQUIRE(fitted.size() == 5);
	REQUIRE(fitted[0] == 10.0);  // First point (no forecast)
	REQUIRE(fitted[1] == 10.0);  // Forecast = previous value
	REQUIRE(fitted[2] == 12.0);
	REQUIRE(fitted[3] == 15.0);
	REQUIRE(fitted[4] == 14.0);
}

TEST_CASE("Naive residuals are first differences", "[models][baseline][naive]") {
	const std::vector<double> data = {10.0, 12.0, 15.0, 14.0, 16.0};
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	Naive model;
	model.fit(ts);
	
	const auto& residuals = model.residuals();
	REQUIRE(residuals.size() == 5);
	REQUIRE(residuals[0] == 0.0);  // No forecast for first
	REQUIRE(residuals[1] == Catch::Approx(2.0));   // 12 - 10
	REQUIRE(residuals[2] == Catch::Approx(3.0));   // 15 - 12
	REQUIRE(residuals[3] == Catch::Approx(-1.0));  // 14 - 15
	REQUIRE(residuals[4] == Catch::Approx(2.0));   // 16 - 14
}

TEST_CASE("Naive confidence intervals", "[models][baseline][naive]") {
	const auto data = generateTrendingData(30);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	Naive model;
	model.fit(ts);
	
	auto forecast = model.predictWithConfidence(5, 0.95);
	
	REQUIRE(forecast.lowerSeries().size() == 5);
	REQUIRE(forecast.upperSeries().size() == 5);
	
	// Intervals should widen with horizon
	REQUIRE(forecast.upperSeries()[4] - forecast.lowerSeries()[4] > 
	        forecast.upperSeries()[0] - forecast.lowerSeries()[0]);
}

TEST_CASE("Naive handles empty data", "[models][baseline][naive][error]") {
	std::vector<double> empty;
	auto ts = tests::helpers::makeUnivariateSeries(empty);
	
	Naive model;
	REQUIRE_THROWS_AS(model.fit(ts), std::invalid_argument);
}

// ==========================
// RandomWalkWithDrift Tests
// ==========================

TEST_CASE("RandomWalkWithDrift calculates drift correctly", "[models][baseline][rwd]") {
	const std::vector<double> data = generateTrendingData(20, 0.5);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	RandomWalkWithDrift model;
	model.fit(ts);
	
	REQUIRE(model.drift() == Catch::Approx(0.5).margin(0.01));
}

TEST_CASE("RandomWalkWithDrift produces trending forecast", "[models][baseline][rwd]") {
	const std::vector<double> data = generateTrendingData(30, 1.0);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	RandomWalkWithDrift model;
	model.fit(ts);
	auto forecast = model.predict(5);
	
	REQUIRE(forecast.primary().size() == 5);
	
	// Should trend upward
	REQUIRE(forecast.primary()[1] > forecast.primary()[0]);
	REQUIRE(forecast.primary()[2] > forecast.primary()[1]);
	REQUIRE(forecast.primary()[3] > forecast.primary()[2]);
	REQUIRE(forecast.primary()[4] > forecast.primary()[3]);
}

TEST_CASE("RandomWalkWithDrift handles zero drift", "[models][baseline][rwd]") {
	const std::vector<double> constant_data(20, 42.0);
	auto ts = tests::helpers::makeUnivariateSeries(constant_data);
	
	RandomWalkWithDrift model;
	model.fit(ts);
	
	REQUIRE(model.drift() == Catch::Approx(0.0).margin(1e-10));
	
	auto forecast = model.predict(5);
	for (double val : forecast.primary()) {
		REQUIRE(val == Catch::Approx(42.0));
	}
}

TEST_CASE("RandomWalkWithDrift short series", "[models][baseline][rwd]") {
	const std::vector<double> short_data = {10.0, 15.0};
	auto ts = tests::helpers::makeUnivariateSeries(short_data);
	
	RandomWalkWithDrift model;
	REQUIRE_NOTHROW(model.fit(ts));
	REQUIRE(model.drift() == Catch::Approx(5.0));
}

TEST_CASE("RandomWalkWithDrift confidence intervals", "[models][baseline][rwd]") {
	// Generate data with some noise to get non-zero residual variance
	std::vector<double> data;
	for (int i = 0; i < 30; ++i) {
		data.push_back(100.0 + 0.5 * i + ((i % 3) - 1) * 0.5);  // Add some variation
	}
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	RandomWalkWithDrift model;
	model.fit(ts);
	
	auto forecast = model.predictWithConfidence(5, 0.95);
	
	REQUIRE(forecast.lowerSeries().size() == 5);
	REQUIRE(forecast.upperSeries().size() == 5);
	
	// Intervals should widen with horizon
	const double width_0 = forecast.upperSeries()[0] - forecast.lowerSeries()[0];
	const double width_4 = forecast.upperSeries()[4] - forecast.lowerSeries()[4];
	REQUIRE(width_4 > width_0);
}

TEST_CASE("RandomWalkWithDrift vs Naive on trending data", "[models][baseline][comparison]") {
	const auto data = generateTrendingData(50, 0.8);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	Naive naive;
	RandomWalkWithDrift rwd;
	
	naive.fit(ts);
	rwd.fit(ts);
	
	auto f_naive = naive.predict(10);
	auto f_rwd = rwd.predict(10);
	
	// RWD should forecast higher than Naive for trending data
	REQUIRE(f_rwd.primary()[9] > f_naive.primary()[9]);
}

// ==========================
// SeasonalNaive Tests
// ==========================

TEST_CASE("SeasonalNaive repeats seasonal pattern", "[models][baseline][seasonal]") {
	// 24 months of data
	std::vector<double> data(24);
	for (int i = 0; i < 24; ++i) {
		data[i] = 100.0 + static_cast<double>(i % 12) * 5.0;
	}
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalNaive model(12);
	model.fit(ts);
	auto forecast = model.predict(12);
	
	REQUIRE(forecast.primary().size() == 12);
	
	// Should repeat last year's pattern
	for (int i = 0; i < 12; ++i) {
		REQUIRE(forecast.primary()[i] == data[12 + i]);
	}
}

TEST_CASE("SeasonalNaive quarterly data", "[models][baseline][seasonal]") {
	const auto data = generateSeasonalData(10, 4);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalNaive model(4);
	model.fit(ts);
	auto forecast = model.predict(8);
	
	REQUIRE(forecast.primary().size() == 8);
	REQUIRE(model.seasonalPeriod() == 4);
}

TEST_CASE("SeasonalNaive weekly data", "[models][baseline][seasonal]") {
	const auto data = generateSeasonalData(10, 7);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalNaive model(7);
	model.fit(ts);
	auto forecast = model.predict(14);
	
	REQUIRE(forecast.primary().size() == 14);
	REQUIRE(model.seasonalPeriod() == 7);
}

TEST_CASE("SeasonalNaive requires full season", "[models][baseline][seasonal][error]") {
	const std::vector<double> short_data = {10.0, 12.0, 15.0};
	auto ts = tests::helpers::makeUnivariateSeries(short_data);
	
	SeasonalNaive model(12);
	REQUIRE_THROWS_AS(model.fit(ts), std::invalid_argument);
}

TEST_CASE("SeasonalNaive forecast beyond one season", "[models][baseline][seasonal]") {
	const auto data = generateSeasonalData(3, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalNaive model(12);
	model.fit(ts);
	auto forecast = model.predict(24);  // 2 years
	
	REQUIRE(forecast.primary().size() == 24);
	
	// First year should match last year
	// Second year should also match last year
	for (int i = 0; i < 12; ++i) {
		REQUIRE(forecast.primary()[i] == forecast.primary()[i + 12]);
	}
}

TEST_CASE("SeasonalNaive fitted values", "[models][baseline][seasonal]") {
	const auto data = generateSeasonalData(3, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalNaive model(12);
	model.fit(ts);
	
	const auto& fitted = model.fittedValues();
	const auto& residuals = model.residuals();
	
	REQUIRE(fitted.size() == data.size());
	REQUIRE(residuals.size() == data.size());
}

TEST_CASE("SeasonalNaive confidence intervals", "[models][baseline][seasonal]") {
	const auto data = generateSeasonalData(5, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalNaive model(12);
	model.fit(ts);
	
	auto forecast = model.predictWithConfidence(12, 0.95);
	
	REQUIRE(forecast.lowerSeries().size() == 12);
	REQUIRE(forecast.upperSeries().size() == 12);
}

// ==========================
// SeasonalWindowAverage Tests
// ==========================

TEST_CASE("SeasonalWindowAverage averages seasonal values", "[models][baseline][seasonal][window]") {
	// Create data with known seasonal pattern
	std::vector<double> data;
	// 3 cycles of same pattern: 100, 110, 120, ..., 210
	for (int c = 0; c < 3; ++c) {
		for (int t = 0; t < 12; ++t) {
			data.push_back(100.0 + static_cast<double>(t) * 10.0);
		}
	}
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalWindowAverage model(12, 2);  // Average last 2 years
	model.fit(ts);
	auto forecast = model.predict(12);
	
	REQUIRE(forecast.primary().size() == 12);
	
	// Each month should be averaged from last 2 occurrences
	// Since all cycles are identical, average equals the value
	for (int i = 0; i < 12; ++i) {
		REQUIRE(forecast.primary()[i] == Catch::Approx(100.0 + i * 10.0));
	}
}

TEST_CASE("SeasonalWindowAverage window=1 equals SeasonalNaive", "[models][baseline][seasonal][window]") {
	const auto data = generateSeasonalData(5, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalNaive snaive(12);
	SeasonalWindowAverage swa(12, 1);
	
	snaive.fit(ts);
	swa.fit(ts);
	
	auto f_snaive = snaive.predict(12);
	auto f_swa = swa.predict(12);
	
	REQUIRE(f_snaive.primary().size() == f_swa.primary().size());
	
	// Should be identical
	for (std::size_t i = 0; i < 12; ++i) {
		REQUIRE(f_swa.primary()[i] == Catch::Approx(f_snaive.primary()[i]));
	}
}

TEST_CASE("SeasonalWindowAverage different window sizes", "[models][baseline][seasonal][window]") {
	const auto data = generateSeasonalData(6, 4);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalWindowAverage model_w2(4, 2);
	SeasonalWindowAverage model_w3(4, 3);
	
	REQUIRE_NOTHROW(model_w2.fit(ts));
	REQUIRE_NOTHROW(model_w3.fit(ts));
	
	REQUIRE(model_w2.window() == 2);
	REQUIRE(model_w3.window() == 3);
}

TEST_CASE("SeasonalWindowAverage confidence intervals", "[models][baseline][seasonal][window]") {
	const auto data = generateSeasonalData(8, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalWindowAverage model(12, 3);
	model.fit(ts);
	
	auto forecast = model.predictWithConfidence(12, 0.95);
	
	REQUIRE(forecast.lowerSeries().size() == 12);
	REQUIRE(forecast.upperSeries().size() == 12);
}

TEST_CASE("SeasonalWindowAverage handles limited data", "[models][baseline][seasonal][window]") {
	// Only 2 cycles, but window=3
	const auto data = generateSeasonalData(2, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalWindowAverage model(12, 3);
	REQUIRE_NOTHROW(model.fit(ts));  // Should work, just average available data
	
	auto forecast = model.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("SeasonalWindowAverage smooths vs SeasonalNaive", "[models][baseline][seasonal][comparison]") {
	// Add some noise to make difference visible
	std::vector<double> data;
	for (int c = 0; c < 5; ++c) {
		for (int t = 0; t < 12; ++t) {
			double base = 100.0 + t * 5.0;
			double noise = (c % 2 == 0) ? 5.0 : -5.0;  // Alternating noise
			data.push_back(base + noise);
		}
	}
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalNaive snaive(12);
	SeasonalWindowAverage swa(12, 3);
	
	snaive.fit(ts);
	swa.fit(ts);
	
	// Both should forecast, but averaging should smooth
	auto f_snaive = snaive.predict(12);
	auto f_swa = swa.predict(12);
	
	REQUIRE(f_snaive.primary().size() == 12);
	REQUIRE(f_swa.primary().size() == 12);
}

TEST_CASE("SeasonalWindowAverage quarterly seasonality", "[models][baseline][seasonal][window]") {
	const auto data = generateSeasonalData(10, 4);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	SeasonalWindowAverage model(4, 2);
	model.fit(ts);
	
	auto forecast = model.predict(8);
	REQUIRE(forecast.primary().size() == 8);
}

// ==========================
// SimpleMovingAverage Window=0 Tests
// ==========================

TEST_CASE("SimpleMovingAverage with window=0 uses full history", "[models][baseline][sma]") {
	const std::vector<double> data = {10.0, 20.0, 30.0, 40.0, 50.0};
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	auto model = SimpleMovingAverageBuilder()
		.withWindow(0)  // Use full history
		.build();
	
	model->fit(ts);
	auto forecast = model->predict(3);
	
	const double expected_mean = (10.0 + 20.0 + 30.0 + 40.0 + 50.0) / 5.0;  // 30.0
	REQUIRE(forecast.primary().size() == 3);
	REQUIRE(forecast.primary()[0] == Catch::Approx(expected_mean));
	REQUIRE(forecast.primary()[1] == Catch::Approx(expected_mean));
	REQUIRE(forecast.primary()[2] == Catch::Approx(expected_mean));
}

TEST_CASE("SimpleMovingAverage window=0 backward compatibility", "[models][baseline][sma]") {
	const std::vector<double> data = {10.0, 20.0, 30.0, 40.0, 50.0, 60.0};
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	// Old behavior: window=3
	auto model_old = SimpleMovingAverageBuilder()
		.withWindow(3)
		.build();
	
	model_old->fit(ts);
	auto forecast_old = model_old->predict(2);
	
	// Should average last 3 values: (40+50+60)/3 = 50
	REQUIRE(forecast_old.primary()[0] == Catch::Approx(50.0));
}

TEST_CASE("SimpleMovingAverage window=0 vs window=size", "[models][baseline][sma]") {
	const std::vector<double> data = {5.0, 10.0, 15.0, 20.0};
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	auto model_0 = SimpleMovingAverageBuilder().withWindow(0).build();
	auto model_4 = SimpleMovingAverageBuilder().withWindow(4).build();
	
	model_0->fit(ts);
	model_4->fit(ts);
	
	auto f_0 = model_0->predict(1);
	auto f_4 = model_4->predict(1);
	
	// Both should give same result (full history mean)
	REQUIRE(f_0.primary()[0] == Catch::Approx(f_4.primary()[0]));
	REQUIRE(f_0.primary()[0] == Catch::Approx(12.5));  // (5+10+15+20)/4
}

TEST_CASE("SimpleMovingAverage rejects negative window", "[models][baseline][sma][error]") {
	REQUIRE_THROWS_AS(
		SimpleMovingAverageBuilder().withWindow(-1).build(),
		std::invalid_argument
	);
}

TEST_CASE("SimpleMovingAverage window=0 on empty data", "[models][baseline][sma][error]") {
	std::vector<double> empty;
	auto ts = tests::helpers::makeUnivariateSeries(empty);
	
	auto model = SimpleMovingAverageBuilder().withWindow(0).build();
	REQUIRE_THROWS_AS(model->fit(ts), std::invalid_argument);
}

// ==========================
// Integration Tests
// ==========================

TEST_CASE("All baseline methods on same data", "[models][baseline][integration]") {
	const auto data = generateSeasonalData(5, 12);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	
	Naive naive;
	RandomWalkWithDrift rwd;
	SeasonalNaive snaive(12);
	SeasonalWindowAverage swa(12, 2);
	auto sma = SimpleMovingAverageBuilder().withWindow(0).build();
	
	REQUIRE_NOTHROW(naive.fit(ts));
	REQUIRE_NOTHROW(rwd.fit(ts));
	REQUIRE_NOTHROW(snaive.fit(ts));
	REQUIRE_NOTHROW(swa.fit(ts));
	REQUIRE_NOTHROW(sma->fit(ts));
	
	const int horizon = 12;
	auto f1 = naive.predict(horizon);
	auto f2 = rwd.predict(horizon);
	auto f3 = snaive.predict(horizon);
	auto f4 = swa.predict(horizon);
	auto f5 = sma->predict(horizon);
	
	REQUIRE(f1.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(f2.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(f3.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(f4.primary().size() == static_cast<std::size_t>(horizon));
	REQUIRE(f5.primary().size() == static_cast<std::size_t>(horizon));
}

TEST_CASE("Baseline methods getName returns correct identifiers", "[models][baseline][metadata]") {
	Naive naive;
	RandomWalkWithDrift rwd;
	SeasonalNaive snaive(12);
	SeasonalWindowAverage swa(12, 2);
	
	REQUIRE(naive.getName() == "Naive");
	REQUIRE(rwd.getName() == "RandomWalkWithDrift");
	REQUIRE(snaive.getName() == "SeasonalNaive");
	REQUIRE(swa.getName() == "SeasonalWindowAverage");
}

TEST_CASE("Baseline methods handle single value", "[models][baseline][edge]") {
	const std::vector<double> single = {42.0};
	auto ts = tests::helpers::makeUnivariateSeries(single);
	
	Naive naive;
	REQUIRE_NOTHROW(naive.fit(ts));
	auto f_naive = naive.predict(3);
	REQUIRE(f_naive.primary()[0] == 42.0);
	
	RandomWalkWithDrift rwd;
	REQUIRE_NOTHROW(rwd.fit(ts));
	auto f_rwd = rwd.predict(3);
	REQUIRE(f_rwd.primary()[0] == 42.0);  // Zero drift
}

