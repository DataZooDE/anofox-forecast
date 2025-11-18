#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "anofox-time/models/mstl_forecaster.hpp"
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

// Generate data with multiple seasonalities
std::vector<double> generateMultiSeasonalData(int n, double level = 100.0, double trend = 0.5) {
	std::vector<double> data(n);
	for (int i = 0; i < n; ++i) {
		double seasonal_weekly = 10.0 * std::sin(2.0 * M_PI * i / 7.0);
		double seasonal_monthly = 5.0 * std::sin(2.0 * M_PI * i / 30.0);
		data[i] = level + trend * i + seasonal_weekly + seasonal_monthly;
	}
	return data;
}

} // namespace

// ============================================================================
// Basic Functionality Tests
// ============================================================================

TEST_CASE("MSTL constructor and parameters", "[mstl][basic]") {
	MSTLForecaster mstl({12});
	
	REQUIRE(mstl.getName() == "MSTL");
	REQUIRE(mstl.seasonalPeriods() == std::vector<int>{12});
	REQUIRE(mstl.trendMethod() == MSTLForecaster::TrendMethod::Linear);
}

TEST_CASE("MSTL constructor validates parameters", "[mstl][basic]") {
	REQUIRE_THROWS_AS(MSTLForecaster({}), std::invalid_argument);  // Empty periods
	REQUIRE_THROWS_AS(MSTLForecaster({1}), std::invalid_argument);  // Period too small
	REQUIRE_THROWS_AS(MSTLForecaster({-5}), std::invalid_argument);  // Negative period
}

TEST_CASE("MSTL fit and predict", "[mstl][basic]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({12});
	REQUIRE_NOTHROW(mstl.fit(ts));
	
	auto forecast = mstl.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("MSTL requires fit before predict", "[mstl][error]") {
	MSTLForecaster mstl({12});
	REQUIRE_THROWS_AS(mstl.predict(10), std::runtime_error);
}

TEST_CASE("MSTL requires fit before accessing components", "[mstl][error]") {
	MSTLForecaster mstl({12});
	REQUIRE_THROWS_AS(mstl.components(), std::runtime_error);
}

TEST_CASE("MSTL validates horizon", "[mstl][error]") {
	auto data = generateSeasonalData(48, 12);
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({12});
	mstl.fit(ts);
	
	REQUIRE_THROWS_AS(mstl.predict(0), std::invalid_argument);
	REQUIRE_THROWS_AS(mstl.predict(-5), std::invalid_argument);
}

// ============================================================================
// Multiple Seasonalities Tests
// ============================================================================

TEST_CASE("MSTL single seasonality", "[mstl][seasonality]") {
	auto data = generateSeasonalData(72, 12, 10.0, 0.5, 100.0);
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({12});
	mstl.fit(ts);
	
	const auto& components = mstl.components();
	REQUIRE(components.trend.size() == 72);
	REQUIRE(components.seasonal.size() == 1);
	REQUIRE(components.seasonal[0].size() == 72);
	REQUIRE(components.remainder.size() == 72);
}

TEST_CASE("MSTL multiple seasonalities", "[mstl][seasonality]") {
	auto data = generateMultiSeasonalData(90);
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({7, 30});  // Weekly and monthly
	mstl.fit(ts);
	
	const auto& components = mstl.components();
	REQUIRE(components.seasonal.size() == 2);
	REQUIRE(components.seasonal[0].size() == 90);
	REQUIRE(components.seasonal[1].size() == 90);
}

TEST_CASE("MSTL forecast with multiple seasonalities", "[mstl][seasonality]") {
	auto data = generateMultiSeasonalData(90);
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({7, 30});
	mstl.fit(ts);
	
	auto forecast = mstl.predict(14);  // 2 weeks
	REQUIRE(forecast.primary().size() == 14);
	
	// Forecasts should be reasonable
	for (double f : forecast.primary()) {
		REQUIRE(std::isfinite(f));
		REQUIRE(f > 50.0);   // Should be positive and reasonable
		REQUIRE(f < 200.0);
	}
}

TEST_CASE("MSTL handles 3+ seasonalities", "[mstl][seasonality]") {
	std::vector<double> data(120);
	for (int i = 0; i < 120; ++i) {
		data[i] = 100.0 + 
		          10.0 * std::sin(2.0 * M_PI * i / 7.0) +    // Weekly
		          5.0 * std::sin(2.0 * M_PI * i / 12.0) +    // Monthly
		          3.0 * std::sin(2.0 * M_PI * i / 4.0);      // Quarterly
	}
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({7, 12, 4});
	REQUIRE_NOTHROW(mstl.fit(ts));
	
	const auto& components = mstl.components();
	REQUIRE(components.seasonal.size() == 3);
}

TEST_CASE("MSTL seasonal projection correctness", "[mstl][seasonality]") {
	auto data = generateSeasonalData(48, 12, 10.0, 0.0, 100.0);  // No trend
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({12}, MSTLForecaster::TrendMethod::None);
	mstl.fit(ts);
	
	auto forecast = mstl.predict(24);  // 2 full cycles
	
	// Check seasonal pattern repeats (approximately)
	for (int i = 0; i < 12; ++i) {
		double diff = std::abs(forecast.primary()[i] - forecast.primary()[i + 12]);
		REQUIRE(diff < 5.0);  // Should repeat within tolerance
	}
}

// ============================================================================
// Trend Forecaster Tests
// ============================================================================

TEST_CASE("MSTL linear trend forecaster", "[mstl][trend]") {
	auto data = generateSeasonalData(60, 12, 5.0, 2.0, 100.0);  // Strong trend
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({12}, MSTLForecaster::TrendMethod::Linear);
	mstl.fit(ts);
	
	auto forecast = mstl.predict(12);
	
	// Linear trend should be increasing
	REQUIRE(forecast.primary()[11] > forecast.primary()[0]);
}

TEST_CASE("MSTL SES trend forecaster", "[mstl][trend]") {
	auto data = generateSeasonalData(60, 12, 5.0, 0.5, 100.0);
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({12}, MSTLForecaster::TrendMethod::SES);
	mstl.fit(ts);
	
	auto forecast = mstl.predict(12);
	REQUIRE(forecast.primary().size() == 12);
	
	// SES forecast should be more stable
	double variance = 0.0;
	double mean = 0.0;
	for (double f : forecast.primary()) {
		mean += f;
	}
	mean /= forecast.primary().size();
	
	for (double f : forecast.primary()) {
		variance += (f - mean) * (f - mean);
	}
	variance /= forecast.primary().size();
	
	REQUIRE(std::sqrt(variance) < 50.0);  // Should have low variance
}

TEST_CASE("MSTL Holt trend forecaster", "[mstl][trend]") {
	auto data = generateSeasonalData(60, 12, 5.0, 1.0, 100.0);
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({12}, MSTLForecaster::TrendMethod::Holt);
	mstl.fit(ts);
	
	auto forecast = mstl.predict(12);
	
	// Holt should capture trend
	REQUIRE(forecast.primary()[11] >= forecast.primary()[0] - 10.0);
}

TEST_CASE("MSTL None trend forecaster", "[mstl][trend]") {
	auto data = generateSeasonalData(60, 12, 10.0, 0.0, 100.0);
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({12}, MSTLForecaster::TrendMethod::None);
	mstl.fit(ts);
	
	auto forecast = mstl.predict(12);
	
	// Constant trend - forecasts should vary mainly due to seasonality
	REQUIRE(forecast.primary().size() == 12);
}

// ============================================================================
// Edge Cases Tests
// ============================================================================

TEST_CASE("MSTL short data", "[mstl][edge]") {
	std::vector<double> data = {100., 105., 110., 108., 112., 115., 113., 118., 
	                            120., 122., 117., 121., 125., 128., 123., 127.};
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({4});  // Shorter period
	REQUIRE_NOTHROW(mstl.fit(ts));
	
	auto forecast = mstl.predict(4);
	REQUIRE(forecast.primary().size() == 4);
}

TEST_CASE("MSTL handles insufficient data gracefully", "[mstl][edge]") {
	std::vector<double> data = {100., 105., 110.};
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({12});
	REQUIRE_THROWS_AS(mstl.fit(ts), std::runtime_error);
}

TEST_CASE("MSTL constant data", "[mstl][edge]") {
	std::vector<double> data(40, 100.0);
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({10});
	REQUIRE_NOTHROW(mstl.fit(ts));
	
	auto forecast = mstl.predict(10);
	
	// Should forecast near constant value
	for (double f : forecast.primary()) {
		REQUIRE(std::abs(f - 100.0) < 20.0);
	}
}

TEST_CASE("MSTL with noise", "[mstl][edge]") {
	auto data = generateSeasonalData(60, 12, 10.0, 0.5, 100.0);
	
	// Add noise
	for (size_t i = 0; i < data.size(); ++i) {
		data[i] += ((i % 3) - 1) * 2.0;  // Simple noise
	}
	
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({12});
	REQUIRE_NOTHROW(mstl.fit(ts));
	
	auto forecast = mstl.predict(12);
	REQUIRE(forecast.primary().size() == 12);
}

TEST_CASE("MSTL large horizon", "[mstl][edge]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({12});
	mstl.fit(ts);
	
	auto forecast = mstl.predict(48);  // 4 full cycles
	REQUIRE(forecast.primary().size() == 48);
	
	// All forecasts should be finite
	for (double f : forecast.primary()) {
		REQUIRE(std::isfinite(f));
	}
}

// ============================================================================
// Performance Comparison Tests
// ============================================================================

TEST_CASE("MSTL forecast quality", "[mstl][performance]") {
	auto data = generateSeasonalData(72, 12, 10.0, 0.5, 100.0);
	
	// Split into train/test
	std::vector<double> train_data(data.begin(), data.begin() + 60);
	std::vector<double> test_data(data.begin() + 60, data.end());
	
	auto train_ts = createTimeSeries(train_data);
	
	MSTLForecaster mstl({12});
	mstl.fit(train_ts);
	auto forecast = mstl.predict(12);
	
	// Calculate MAE
	double mae = 0.0;
	for (size_t i = 0; i < test_data.size(); ++i) {
		mae += std::abs(forecast.primary()[i] - test_data[i]);
	}
	mae /= test_data.size();
	
	// Should have reasonable accuracy
	REQUIRE(mae < 15.0);
}

TEST_CASE("MSTL different trend methods comparison", "[mstl][performance]") {
	auto data = generateSeasonalData(60, 12, 10.0, 1.0, 100.0);
	auto ts = createTimeSeries(data);
	
	std::vector<MSTLForecaster::TrendMethod> methods = {
		MSTLForecaster::TrendMethod::Linear,
		MSTLForecaster::TrendMethod::SES,
		MSTLForecaster::TrendMethod::Holt,
		MSTLForecaster::TrendMethod::None
	};
	
	for (auto method : methods) {
		MSTLForecaster mstl({12}, method);
		REQUIRE_NOTHROW(mstl.fit(ts));
		
		auto forecast = mstl.predict(12);
		REQUIRE(forecast.primary().size() == 12);
		
		// All should produce finite forecasts
		for (double f : forecast.primary()) {
			REQUIRE(std::isfinite(f));
		}
	}
}

TEST_CASE("MSTL speed test", "[mstl][performance]") {
	auto data = generateSeasonalData(120, 12);
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl({12});
	
	auto start = std::chrono::high_resolution_clock::now();
	mstl.fit(ts);
	auto forecast = mstl.predict(12);
	auto end = std::chrono::high_resolution_clock::now();
	
	auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
	
	// Should be fast (< 100ms for this size)
	REQUIRE(duration.count() < 100);
}

TEST_CASE("MSTL robust option", "[mstl][performance]") {
	auto data = generateSeasonalData(60, 12, 10.0, 0.5, 100.0);
	
	// Add outliers
	data[10] = 200.0;
	data[30] = 50.0;
	
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl_regular({12}, MSTLForecaster::TrendMethod::Linear, MSTLForecaster::SeasonalMethod::Cyclic, MSTLForecaster::DeseasonalizedForecastMethod::ExponentialSmoothing, 2, false);
	MSTLForecaster mstl_robust({12}, MSTLForecaster::TrendMethod::Linear, MSTLForecaster::SeasonalMethod::Cyclic, MSTLForecaster::DeseasonalizedForecastMethod::ExponentialSmoothing, 2, true);
	
	REQUIRE_NOTHROW(mstl_regular.fit(ts));
	REQUIRE_NOTHROW(mstl_robust.fit(ts));
	
	auto forecast_regular = mstl_regular.predict(12);
	auto forecast_robust = mstl_robust.predict(12);
	
	// Both should produce reasonable forecasts
	REQUIRE(forecast_regular.primary().size() == 12);
	REQUIRE(forecast_robust.primary().size() == 12);
}

TEST_CASE("MSTL multiple iterations", "[mstl][performance]") {
	auto data = generateSeasonalData(60, 12);
	auto ts = createTimeSeries(data);
	
	MSTLForecaster mstl_1iter({12}, MSTLForecaster::TrendMethod::Linear, MSTLForecaster::SeasonalMethod::Cyclic, MSTLForecaster::DeseasonalizedForecastMethod::ExponentialSmoothing, 1);
	MSTLForecaster mstl_3iter({12}, MSTLForecaster::TrendMethod::Linear, MSTLForecaster::SeasonalMethod::Cyclic, MSTLForecaster::DeseasonalizedForecastMethod::ExponentialSmoothing, 3);
	
	mstl_1iter.fit(ts);
	mstl_3iter.fit(ts);
	
	auto forecast_1 = mstl_1iter.predict(12);
	auto forecast_3 = mstl_3iter.predict(12);
	
	// Both should work
	REQUIRE(forecast_1.primary().size() == 12);
	REQUIRE(forecast_3.primary().size() == 12);
}

// ============================================================================
// Builder Tests
// ============================================================================

TEST_CASE("MSTL builder pattern", "[mstl][builder]") {
	auto mstl = MSTLForecasterBuilder()
		.withSeasonalPeriods({7, 12})
		.withTrendMethod(MSTLForecaster::TrendMethod::Holt)
		.withMSTLIterations(3)
		.withRobust(true)
		.build();
	
	REQUIRE(mstl->getName() == "MSTL");
	REQUIRE(mstl->seasonalPeriods() == std::vector<int>{7, 12});
	REQUIRE(mstl->trendMethod() == MSTLForecaster::TrendMethod::Holt);
}

TEST_CASE("MSTL builder default values", "[mstl][builder]") {
	auto mstl = MSTLForecasterBuilder().build();
	
	REQUIRE(mstl->seasonalPeriods() == std::vector<int>{12});
	REQUIRE(mstl->trendMethod() == MSTLForecaster::TrendMethod::Linear);
}

