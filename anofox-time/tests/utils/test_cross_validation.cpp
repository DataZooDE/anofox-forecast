#include <catch2/catch_test_macros.hpp>
#include "anofox-time/utils/cross_validation.hpp"
#include "anofox-time/core/time_series.hpp"
#include "anofox-time/models/sma.hpp"
#include "anofox-time/models/naive.hpp"
#include <chrono>
#include <vector>
#include <memory>

using namespace anofoxtime::utils;
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

} // namespace

TEST_CASE("CrossValidation generateFolds expanding window", "[utils][cross_validation]") {
	CVConfig config;
	config.strategy = CVStrategy::EXPANDING;
	config.initial_window = 10;
	config.horizon = 5;
	config.step = 5;
	
	auto folds = CrossValidation::generateFolds(30, config);
	
	REQUIRE(folds.size() > 0);
	// First fold should start at initial_window
	auto [train_start, train_end, test_start, test_end] = folds[0];
	REQUIRE(train_start == 0);
	REQUIRE(train_end == 10);
	REQUIRE(test_start == 10);
	REQUIRE(test_end == 15);
}

TEST_CASE("CrossValidation generateFolds rolling window", "[utils][cross_validation]") {
	CVConfig config;
	config.strategy = CVStrategy::ROLLING;
	config.initial_window = 10;
	config.horizon = 5;
	config.step = 5;
	config.max_window = 0;  // Use initial_window size
	
	auto folds = CrossValidation::generateFolds(30, config);
	
	REQUIRE(folds.size() > 0);
	auto [train_start, train_end, test_start, test_end] = folds[0];
	REQUIRE(train_start == 0);
	REQUIRE(train_end == 10);
}

TEST_CASE("CrossValidation generateFolds with max_window", "[utils][cross_validation]") {
	CVConfig config;
	config.strategy = CVStrategy::ROLLING;
	config.initial_window = 10;
	config.horizon = 5;
	config.step = 5;
	config.max_window = 15;  // Limit window size
	
	auto folds = CrossValidation::generateFolds(50, config);
	
	REQUIRE(folds.size() > 0);
	// Later folds should respect max_window
	if (folds.size() > 1) {
		auto [train_start, train_end, test_start, test_end] = folds[1];
		REQUIRE(train_end - train_start <= 15);
	}
}

TEST_CASE("CrossValidation generateFolds with insufficient data", "[utils][cross_validation][error]") {
	CVConfig config;
	config.initial_window = 10;
	config.horizon = 5;
	
	REQUIRE_THROWS_AS(CrossValidation::generateFolds(12, config), std::invalid_argument);
}

TEST_CASE("CrossValidation generateFolds step size", "[utils][cross_validation]") {
	CVConfig config;
	config.strategy = CVStrategy::EXPANDING;
	config.initial_window = 10;
	config.horizon = 5;
	config.step = 10;  // Larger step
	
	auto folds = CrossValidation::generateFolds(40, config);
	
	// With step=10, should have fewer folds than step=5
	CVConfig config_small_step = config;
	config_small_step.step = 5;
	auto folds_small = CrossValidation::generateFolds(40, config_small_step);
	
	REQUIRE(folds.size() < folds_small.size());
}

TEST_CASE("CrossValidation evaluate basic", "[utils][cross_validation]") {
	auto data = createTimeSeries({1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0,
	                              11.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0});
	
	CVConfig config;
	config.strategy = CVStrategy::EXPANDING;
	config.initial_window = 10;
	config.horizon = 3;
	config.step = 5;
	
	// Use a simple model factory (SMA)
	auto model_factory = []() {
		return anofoxtime::models::SimpleMovingAverageBuilder().withWindow(3).build();
	};
	
	auto results = CrossValidation::evaluate(data, model_factory, config);
	
	REQUIRE(results.folds.size() > 0);
	REQUIRE(std::isfinite(results.mae) || results.total_forecasts == 0);
}

TEST_CASE("CrossValidation evaluate with short series", "[utils][cross_validation]") {
	auto data = createTimeSeries({1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0,
	                              11.0, 12.0, 13.0, 14.0, 15.0});
	
	CVConfig config;
	config.initial_window = 8;
	config.horizon = 2;
	config.step = 3;
	
	auto model_factory = []() {
		return anofoxtime::models::SimpleMovingAverageBuilder().withWindow(2).build();
	};
	
	auto results = CrossValidation::evaluate(data, model_factory, config);
	REQUIRE(results.folds.size() > 0);
}

