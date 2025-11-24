#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "anofox-time/utils/intermittent_utils.hpp"
#include <vector>
#include <cmath>
#include <limits>

using namespace anofoxtime::utils::intermittent;

TEST_CASE("extractDemand filters zero values", "[utils][intermittent]") {
	std::vector<double> data{0.0, 1.0, 0.0, 2.0, 3.0, 0.0};
	auto demand = extractDemand(data);
	
	REQUIRE(demand.size() == 3);
	REQUIRE(demand[0] == 1.0);
	REQUIRE(demand[1] == 2.0);
	REQUIRE(demand[2] == 3.0);
}

TEST_CASE("extractDemand handles all zeros", "[utils][intermittent]") {
	std::vector<double> data{0.0, 0.0, 0.0};
	auto demand = extractDemand(data);
	
	REQUIRE(demand.empty());
}

TEST_CASE("extractDemand handles all positive", "[utils][intermittent]") {
	std::vector<double> data{1.0, 2.0, 3.0};
	auto demand = extractDemand(data);
	
	REQUIRE(demand.size() == 3);
	REQUIRE(demand == data);
}

TEST_CASE("extractDemand handles empty input", "[utils][intermittent]") {
	std::vector<double> empty;
	auto demand = extractDemand(empty);
	
	REQUIRE(demand.empty());
}

TEST_CASE("computeIntervals calculates intervals correctly", "[utils][intermittent]") {
	std::vector<double> data{0.0, 1.0, 0.0, 0.0, 2.0, 0.0, 3.0};
	auto intervals = computeIntervals(data);
	
	REQUIRE(intervals.size() == 3);
	REQUIRE(intervals[0] == 2.0);  // First nonzero at index 1 (1-based)
	REQUIRE(intervals[1] == 3.0);  // Difference: 5 - 2 = 3
	REQUIRE(intervals[2] == 2.0);  // Difference: 7 - 5 = 2
}

TEST_CASE("computeIntervals handles all zeros", "[utils][intermittent]") {
	std::vector<double> data{0.0, 0.0, 0.0};
	auto intervals = computeIntervals(data);
	
	REQUIRE(intervals.empty());
}

TEST_CASE("computeIntervals handles consecutive nonzeros", "[utils][intermittent]") {
	std::vector<double> data{1.0, 2.0, 3.0};
	auto intervals = computeIntervals(data);
	
	REQUIRE(intervals.size() == 3);
	REQUIRE(intervals[0] == 1.0);  // First at index 0 (1-based = 1)
	REQUIRE(intervals[1] == 1.0);  // Difference: 2 - 1 = 1
	REQUIRE(intervals[2] == 1.0);  // Difference: 3 - 2 = 1
}

TEST_CASE("computeProbability converts to binary", "[utils][intermittent]") {
	std::vector<double> data{0.0, 1.0, 0.0, 5.0, 0.0};
	auto probability = computeProbability(data);
	
	REQUIRE(probability.size() == 5);
	REQUIRE(probability[0] == 0.0);
	REQUIRE(probability[1] == 1.0);
	REQUIRE(probability[2] == 0.0);
	REQUIRE(probability[3] == 1.0);
	REQUIRE(probability[4] == 0.0);
}

TEST_CASE("computeProbability handles empty input", "[utils][intermittent]") {
	std::vector<double> empty;
	auto probability = computeProbability(empty);
	
	REQUIRE(probability.empty());
}

TEST_CASE("sesForecasting basic", "[utils][intermittent]") {
	std::vector<double> data{1.0, 2.0, 3.0, 4.0};
	auto [forecast, fitted] = sesForecasting(data, 0.5);
	
	REQUIRE(std::isfinite(forecast));
	REQUIRE(fitted.size() == 4);
	REQUIRE(std::isnan(fitted[0]));  // First value set to NaN
	REQUIRE(std::isfinite(fitted[1]));
}

TEST_CASE("sesForecasting with empty input", "[utils][intermittent]") {
	std::vector<double> empty;
	auto [forecast, fitted] = sesForecasting(empty, 0.5);
	
	REQUIRE(forecast == 0.0);
	REQUIRE(fitted.empty());
}

TEST_CASE("sesForecasting with single value", "[utils][intermittent]") {
	std::vector<double> data{5.0};
	auto [forecast, fitted] = sesForecasting(data, 0.5);
	
	REQUIRE(std::isfinite(forecast));
	REQUIRE(fitted.size() == 1);
	REQUIRE(std::isnan(fitted[0]));
}

TEST_CASE("optimizedSesForecasting basic", "[utils][intermittent]") {
	std::vector<double> data{1.0, 2.0, 3.0, 4.0, 5.0};
	auto [forecast, fitted] = optimizedSesForecasting(data, 0.1, 0.9);
	
	REQUIRE(std::isfinite(forecast));
	REQUIRE(fitted.size() == 5);
}

TEST_CASE("optimizedSesForecasting with empty input", "[utils][intermittent]") {
	std::vector<double> empty;
	auto [forecast, fitted] = optimizedSesForecasting(empty, 0.1, 0.9);
	
	REQUIRE(forecast == 0.0);
	REQUIRE(fitted.empty());
}

TEST_CASE("chunkSums basic", "[utils][intermittent]") {
	std::vector<double> data{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
	auto sums = chunkSums(data, 2);
	
	REQUIRE(sums.size() == 3);
	REQUIRE(sums[0] == Catch::Approx(3.0));  // 1 + 2
	REQUIRE(sums[1] == Catch::Approx(7.0));  // 3 + 4
	REQUIRE(sums[2] == Catch::Approx(11.0)); // 5 + 6
}

TEST_CASE("chunkSums with remainder", "[utils][intermittent]") {
	std::vector<double> data{1.0, 2.0, 3.0, 4.0, 5.0};
	auto sums = chunkSums(data, 2);
	
	REQUIRE(sums.size() == 2);  // Only complete chunks
	REQUIRE(sums[0] == Catch::Approx(3.0));
	REQUIRE(sums[1] == Catch::Approx(7.0));
}

TEST_CASE("chunkSums with invalid chunk_size", "[utils][intermittent]") {
	std::vector<double> data{1.0, 2.0, 3.0};
	auto sums = chunkSums(data, 0);
	
	REQUIRE(sums.empty());
}

TEST_CASE("chunkSums with empty input", "[utils][intermittent]") {
	std::vector<double> empty;
	auto sums = chunkSums(empty, 2);
	
	REQUIRE(sums.empty());
}

TEST_CASE("expandFittedDemand basic", "[utils][intermittent]") {
	std::vector<double> fitted{10.0, 20.0, 30.0};
	std::vector<double> y{0.0, 5.0, 0.0, 0.0, 8.0, 0.0, 12.0};
	auto expanded = expandFittedDemand(fitted, y);
	
	// When last element is nonzero, expanded needs to be size y.size() + 1 to accommodate expanded[y.size()]
	REQUIRE(expanded.size() == y.size() + 1);
	REQUIRE(std::isnan(expanded[0]));
	REQUIRE(expanded[2] == Catch::Approx(10.0));  // After first nonzero
	REQUIRE(expanded[5] == Catch::Approx(20.0));  // After second nonzero
	REQUIRE(expanded[7] == Catch::Approx(30.0));  // After third nonzero
}

TEST_CASE("expandFittedDemand with empty fitted", "[utils][intermittent]") {
	std::vector<double> fitted;
	std::vector<double> y{0.0, 1.0, 0.0};
	auto expanded = expandFittedDemand(fitted, y);
	
	REQUIRE(expanded.size() == y.size());
	REQUIRE(std::isnan(expanded[0]));
	REQUIRE(std::isnan(expanded[1]));
	REQUIRE(std::isnan(expanded[2]));
}

TEST_CASE("expandFittedIntervals basic", "[utils][intermittent]") {
	std::vector<double> fitted{2.0, 3.0, 2.0};
	std::vector<double> y{0.0, 1.0, 0.0, 0.0, 2.0, 0.0, 3.0};
	auto expanded = expandFittedIntervals(fitted, y);
	
	// When last element is nonzero, expanded needs to be size y.size() + 1 to accommodate expanded[y.size()]
	REQUIRE(expanded.size() == y.size() + 1);
	REQUIRE(std::isnan(expanded[0]));
	REQUIRE(expanded[2] == Catch::Approx(2.0));
	REQUIRE(expanded[5] == Catch::Approx(3.0));
	REQUIRE(expanded[7] == Catch::Approx(2.0));
}

TEST_CASE("chunkForecast basic", "[utils][intermittent]") {
	std::vector<double> data{1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0};
	double forecast = chunkForecast(data, 2);
	
	REQUIRE(std::isfinite(forecast));
	REQUIRE(forecast >= 0.0);
}

TEST_CASE("chunkForecast with invalid aggregation_level", "[utils][intermittent]") {
	std::vector<double> data{1.0, 2.0, 3.0};
	double forecast = chunkForecast(data, 0);
	
	REQUIRE(forecast == 0.0);
}

TEST_CASE("chunkForecast with empty input", "[utils][intermittent]") {
	std::vector<double> empty;
	double forecast = chunkForecast(empty, 2);
	
	REQUIRE(forecast == 0.0);
}

TEST_CASE("chunkForecast with remainder", "[utils][intermittent]") {
	std::vector<double> data{1.0, 2.0, 3.0, 4.0, 5.0};
	double forecast = chunkForecast(data, 2);
	
	// Should discard remainder (last value) and use first 4
	REQUIRE(std::isfinite(forecast));
}

