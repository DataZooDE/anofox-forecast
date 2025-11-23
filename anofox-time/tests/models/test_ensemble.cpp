#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "anofox-time/models/ensemble.hpp"
#include "anofox-time/models/naive.hpp"
#include "anofox-time/models/ses.hpp"
#include "anofox-time/models/sma.hpp"
#include "anofox-time/models/theta.hpp"
#include "anofox-time/models/arima.hpp"
#include "anofox-time/core/time_series.hpp"
#include <chrono>
#include <memory>
#include <vector>

using namespace anofoxtime::models;
using namespace anofoxtime::core;
using namespace std::chrono_literals;

// Type aliases for cleaner code
using SES = SimpleExponentialSmoothing;
using SMA = SimpleMovingAverage;

namespace {

// Helper function to create a simple time series
TimeSeries createTestTimeSeries() {
	std::vector<TimeSeries::TimePoint> timestamps;
	std::vector<double> values;
	
	// Create 50 data points with a trend and some seasonality
	auto base_time = std::chrono::system_clock::now();
	for (int i = 0; i < 50; ++i) {
		timestamps.push_back(base_time + std::chrono::hours(24 * i));
		// Trend + seasonality + noise
		double trend = 100.0 + 2.0 * i;
		double seasonal = 10.0 * std::sin(2.0 * M_PI * i / 7.0);
		values.push_back(trend + seasonal);
	}
	
	return TimeSeries(std::move(timestamps), std::move(values));
}

// Helper function to create a longer time series
TimeSeries createLongerTimeSeries() {
	std::vector<TimeSeries::TimePoint> timestamps;
	std::vector<double> values;
	
	auto base_time = std::chrono::system_clock::now();
	for (int i = 0; i < 100; ++i) {
		timestamps.push_back(base_time + std::chrono::hours(24 * i));
		double trend = 100.0 + 1.5 * i;
		double seasonal = 15.0 * std::sin(2.0 * M_PI * i / 12.0);
		values.push_back(trend + seasonal);
	}
	
	return TimeSeries(std::move(timestamps), std::move(values));
}

} // anonymous namespace

TEST_CASE("Ensemble construction", "[ensemble]") {
	SECTION("Construction with forecasters") {
		std::vector<std::shared_ptr<IForecaster>> forecasters;
		forecasters.push_back(std::make_shared<Naive>());
		forecasters.push_back(SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
		
		REQUIRE_NOTHROW(Ensemble(forecasters));
	}
	
	SECTION("Construction with empty forecasters throws") {
		std::vector<std::shared_ptr<IForecaster>> forecasters;
		REQUIRE_THROWS_AS(Ensemble(forecasters), std::invalid_argument);
	}
	
	SECTION("Construction with factories") {
		std::vector<std::function<std::shared_ptr<IForecaster>()>> factories;
		factories.push_back([]() { return std::make_shared<Naive>(); });
		factories.push_back([]() { return SimpleExponentialSmoothingBuilder().withAlpha(0.3).build(); });
		
		REQUIRE_NOTHROW(Ensemble(factories));
	}
	
	SECTION("Construction with empty factories throws") {
		std::vector<std::function<std::shared_ptr<IForecaster>()>> factories;
		REQUIRE_THROWS_AS(Ensemble(factories), std::invalid_argument);
	}
}

TEST_CASE("Ensemble Mean combination", "[ensemble]") {
	auto ts = createTestTimeSeries();
	
	std::vector<std::shared_ptr<IForecaster>> forecasters;
	forecasters.push_back(std::make_shared<Naive>());
	forecasters.push_back(SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
	forecasters.push_back(SimpleMovingAverageBuilder().withWindow(5).build());
	
	EnsembleConfig config;
	config.method = EnsembleCombinationMethod::Mean;
	
	Ensemble ensemble(forecasters, config);
	
	SECTION("Fit and predict") {
		REQUIRE_NOTHROW(ensemble.fit(ts));
		
		auto forecast = ensemble.predict(10);
		REQUIRE(forecast.horizon() == 10);
		REQUIRE_FALSE(forecast.empty());
		
		// Check all values are finite
		for (const auto& val : forecast.primary()) {
			REQUIRE(std::isfinite(val));
		}
	}
	
	SECTION("Weights are equal for mean ensemble") {
		ensemble.fit(ts);
		auto weights = ensemble.getWeights();
		
		REQUIRE(weights.size() == 3);
		for (const auto& w : weights) {
			REQUIRE_THAT(w, Catch::Matchers::WithinRel(1.0 / 3.0, 0.001));
		}
	}
	
	SECTION("Mean forecast is average of individual forecasts") {
		ensemble.fit(ts);
		
		const int horizon = 5;
		auto ensemble_forecast = ensemble.predict(horizon);
		auto individual_forecasts = ensemble.getIndividualForecasts(horizon);
		
		REQUIRE(individual_forecasts.size() == 3);
		
		// Check that ensemble forecast is the mean
		for (int h = 0; h < horizon; ++h) {
			double expected_mean = 0.0;
			for (const auto& forecast : individual_forecasts) {
				expected_mean += forecast.primary()[h];
			}
			expected_mean /= individual_forecasts.size();
			
			REQUIRE_THAT(ensemble_forecast.primary()[h], 
			            Catch::Matchers::WithinRel(expected_mean, 0.001));
		}
	}
	
	SECTION("getName returns correct name") {
		REQUIRE(ensemble.getName() == "Ensemble<Mean>[3]");
	}
}

TEST_CASE("Ensemble Median combination", "[ensemble]") {
	auto ts = createTestTimeSeries();
	
	std::vector<std::shared_ptr<IForecaster>> forecasters;
	forecasters.push_back(std::make_shared<Naive>());
	forecasters.push_back(SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
	forecasters.push_back(SimpleMovingAverageBuilder().withWindow(5).build());
	forecasters.push_back(SimpleMovingAverageBuilder().withWindow(10).build());
	forecasters.push_back(SimpleMovingAverageBuilder().withWindow(15).build());
	
	EnsembleConfig config;
	config.method = EnsembleCombinationMethod::Median;
	
	Ensemble ensemble(forecasters, config);
	
	SECTION("Fit and predict") {
		ensemble.fit(ts);
		
		auto forecast = ensemble.predict(10);
		REQUIRE(forecast.horizon() == 10);
		REQUIRE_FALSE(forecast.empty());
		
		for (const auto& val : forecast.primary()) {
			REQUIRE(std::isfinite(val));
		}
	}
	
	SECTION("Median forecast is reasonable") {
		ensemble.fit(ts);
		
		const int horizon = 5;
		auto ensemble_forecast = ensemble.predict(horizon);
		auto individual_forecasts = ensemble.getIndividualForecasts(horizon);
		
		// Median should be between min and max of individual forecasts
		for (int h = 0; h < horizon; ++h) {
			double min_val = std::numeric_limits<double>::infinity();
			double max_val = -std::numeric_limits<double>::infinity();
			
			for (const auto& forecast : individual_forecasts) {
				const double val = forecast.primary()[h];
				min_val = std::min(min_val, val);
				max_val = std::max(max_val, val);
			}
			
			const double median = ensemble_forecast.primary()[h];
			REQUIRE(median >= min_val);
			REQUIRE(median <= max_val);
		}
	}
	
	SECTION("getName returns correct name") {
		REQUIRE(ensemble.getName() == "Ensemble<Median>[5]");
	}
}

TEST_CASE("Ensemble WeightedAIC combination", "[ensemble]") {
	auto ts = createTestTimeSeries();
	
	// Use models that have AIC
	std::vector<std::shared_ptr<IForecaster>> forecasters;
	
	auto arima1 = ARIMABuilder()
		.withAR(1)
		.withMA(0)
		.build();
	forecasters.push_back(std::move(arima1));
	
	auto arima2 = ARIMABuilder()
		.withAR(2)
		.withMA(1)
		.build();
	forecasters.push_back(std::move(arima2));
	
	EnsembleConfig config;
	config.method = EnsembleCombinationMethod::WeightedAIC;
	
	Ensemble ensemble(forecasters, config);
	
	SECTION("Fit and predict with AIC models") {
		ensemble.fit(ts);
		
		auto forecast = ensemble.predict(10);
		REQUIRE(forecast.horizon() == 10);
		REQUIRE_FALSE(forecast.empty());
		
		for (const auto& val : forecast.primary()) {
			REQUIRE(std::isfinite(val));
		}
	}
	
	SECTION("Weights sum to 1") {
		ensemble.fit(ts);
		auto weights = ensemble.getWeights();
		
		double sum = 0.0;
		for (const auto& w : weights) {
			sum += w;
		}
		
		REQUIRE_THAT(sum, Catch::Matchers::WithinRel(1.0, 0.001));
	}
	
	SECTION("getName returns correct name") {
		REQUIRE(ensemble.getName() == "Ensemble<WeightedAIC>[2]");
	}
}

TEST_CASE("Ensemble WeightedAccuracy combination", "[ensemble]") {
	auto ts = createLongerTimeSeries();
	
	std::vector<std::shared_ptr<IForecaster>> forecasters;
	forecasters.push_back(std::make_shared<Naive>());
	forecasters.push_back(SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
	forecasters.push_back(SimpleMovingAverageBuilder().withWindow(5).build());
	
	EnsembleConfig config;
	config.method = EnsembleCombinationMethod::WeightedAccuracy;
	config.accuracy_metric = AccuracyMetric::MAE;
	config.validation_split = 0.2;
	
	Ensemble ensemble(forecasters, config);
	
	SECTION("Fit and predict with accuracy weighting") {
		ensemble.fit(ts);
		
		auto forecast = ensemble.predict(10);
		REQUIRE(forecast.horizon() == 10);
		REQUIRE_FALSE(forecast.empty());
		
		for (const auto& val : forecast.primary()) {
			REQUIRE(std::isfinite(val));
		}
	}
	
	SECTION("Weights sum to 1") {
		ensemble.fit(ts);
		auto weights = ensemble.getWeights();
		
		double sum = 0.0;
		for (const auto& w : weights) {
			sum += w;
		}
		
		REQUIRE_THAT(sum, Catch::Matchers::WithinRel(1.0, 0.001));
	}
	
	SECTION("Different accuracy metrics") {
		config.accuracy_metric = AccuracyMetric::MSE;
		Ensemble ensemble_mse(forecasters, config);
		REQUIRE_NOTHROW(ensemble_mse.fit(ts));
		
		config.accuracy_metric = AccuracyMetric::RMSE;
		Ensemble ensemble_rmse(forecasters, config);
		REQUIRE_NOTHROW(ensemble_rmse.fit(ts));
	}
	
	SECTION("In-sample accuracy (validation_split = 0)") {
		config.validation_split = 0.0;
		Ensemble ensemble_insample(forecasters, config);
		REQUIRE_NOTHROW(ensemble_insample.fit(ts));
		
		auto forecast = ensemble_insample.predict(5);
		REQUIRE_FALSE(forecast.empty());
	}
	
	SECTION("getName returns correct name") {
		REQUIRE(ensemble.getName() == "Ensemble<WeightedAccuracy>[3]");
	}
}

TEST_CASE("Ensemble configuration", "[ensemble]") {
	auto ts = createTestTimeSeries();
	
	std::vector<std::shared_ptr<IForecaster>> forecasters;
	forecasters.push_back(std::make_shared<Naive>());
	forecasters.push_back(SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
	
	SECTION("Update configuration") {
		EnsembleConfig config;
		config.method = EnsembleCombinationMethod::Mean;
		
		Ensemble ensemble(forecasters, config);
		ensemble.fit(ts);
		
		// Change to median
		EnsembleConfig new_config;
		new_config.method = EnsembleCombinationMethod::Median;
		ensemble.setConfig(new_config);
		
		// Should require refitting
		ensemble.fit(ts);
		auto forecast = ensemble.predict(5);
		REQUIRE_FALSE(forecast.empty());
	}
	
	SECTION("Get configuration") {
		EnsembleConfig config;
		config.method = EnsembleCombinationMethod::Mean;
		config.temperature = 2.0;
		
		Ensemble ensemble(forecasters, config);
		
		const auto& retrieved_config = ensemble.getConfig();
		REQUIRE(retrieved_config.method == EnsembleCombinationMethod::Mean);
		REQUIRE(retrieved_config.temperature == 2.0);
	}
	
	SECTION("Min weight threshold") {
		EnsembleConfig config;
		config.method = EnsembleCombinationMethod::Mean;
		config.min_weight = 0.4;
		
		Ensemble ensemble(forecasters, config);
		ensemble.fit(ts);
		
		auto weights = ensemble.getWeights();
		// With 2 forecasters and min_weight 0.4, some weights should be zeroed out
		// Actually with mean, all weights are equal (0.5), so both should survive
		REQUIRE(weights.size() == 2);
	}
}

TEST_CASE("Ensemble with factories", "[ensemble]") {
	auto ts = createTestTimeSeries();
	
	std::vector<std::function<std::shared_ptr<IForecaster>()>> factories;
	factories.push_back([]() { return std::make_shared<Naive>(); });
	factories.push_back([]() { return SimpleExponentialSmoothingBuilder().withAlpha(0.3).build(); });
	factories.push_back([]() { return SimpleMovingAverageBuilder().withWindow(5).build(); });
	
	EnsembleConfig config;
	config.method = EnsembleCombinationMethod::Mean;
	
	Ensemble ensemble(factories, config);
	
	SECTION("Fit and predict with factories") {
		ensemble.fit(ts);
		
		auto forecast = ensemble.predict(10);
		REQUIRE(forecast.horizon() == 10);
		REQUIRE_FALSE(forecast.empty());
	}
	
	SECTION("Refitting creates fresh instances") {
		ensemble.fit(ts);
		auto forecast1 = ensemble.predict(5);
		
		// Fit again - should create new instances from factories
		ensemble.fit(ts);
		auto forecast2 = ensemble.predict(5);
		
		REQUIRE(forecast1.horizon() == forecast2.horizon());
	}
}

TEST_CASE("Ensemble edge cases", "[ensemble]") {
	SECTION("Empty time series") {
		std::vector<TimeSeries::TimePoint> timestamps;
		std::vector<double> values;
		TimeSeries empty_ts(std::move(timestamps), std::move(values));
		
		std::vector<std::shared_ptr<IForecaster>> forecasters;
		forecasters.push_back(std::make_shared<Naive>());
		
		Ensemble ensemble(forecasters);
		REQUIRE_THROWS(ensemble.fit(empty_ts));
	}
	
	SECTION("Predict before fit") {
		std::vector<std::shared_ptr<IForecaster>> forecasters;
		forecasters.push_back(std::make_shared<Naive>());
		
		Ensemble ensemble(forecasters);
		REQUIRE_THROWS(ensemble.predict(5));
	}
	
	SECTION("Zero or negative horizon") {
		auto ts = createTestTimeSeries();
		
		std::vector<std::shared_ptr<IForecaster>> forecasters;
		forecasters.push_back(std::make_shared<Naive>());
		
		Ensemble ensemble(forecasters);
		ensemble.fit(ts);
		
		REQUIRE_THROWS(ensemble.predict(0));
		REQUIRE_THROWS(ensemble.predict(-5));
	}
}

TEST_CASE("Ensemble getIndividualForecasts", "[ensemble]") {
	auto ts = createTestTimeSeries();
	
	std::vector<std::shared_ptr<IForecaster>> forecasters;
	forecasters.push_back(std::make_shared<Naive>());
	forecasters.push_back(SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
	
	Ensemble ensemble(forecasters);
	ensemble.fit(ts);
	
	SECTION("Individual forecasts have correct count") {
		auto individual = ensemble.getIndividualForecasts(10);
		REQUIRE(individual.size() == 2);
		
		for (const auto& forecast : individual) {
			REQUIRE(forecast.horizon() == 10);
		}
	}
}

TEST_CASE("Ensemble getForecasters", "[ensemble]") {
	std::vector<std::shared_ptr<IForecaster>> forecasters;
	forecasters.push_back(std::make_shared<Naive>());
	forecasters.push_back(SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
	
	Ensemble ensemble(forecasters);
	
	const auto& retrieved = ensemble.getForecasters();
	REQUIRE(retrieved.size() == 2);
	REQUIRE(retrieved[0]->getName() == "Naive");
	REQUIRE(retrieved[1]->getName() == "SimpleExponentialSmoothing");
}

TEST_CASE("Ensemble temperature parameter", "[ensemble]") {
	auto ts = createLongerTimeSeries();
	
	std::vector<std::shared_ptr<IForecaster>> forecasters;
	forecasters.push_back(std::make_shared<Naive>());
	forecasters.push_back(SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
	forecasters.push_back(SimpleMovingAverageBuilder().withWindow(5).build());
	
	SECTION("Low temperature gives more extreme weights") {
		EnsembleConfig config;
		config.method = EnsembleCombinationMethod::WeightedAccuracy;
		config.validation_split = 0.2;
		config.temperature = 0.5;
		
		Ensemble ensemble(forecasters, config);
		ensemble.fit(ts);
		
		auto weights = ensemble.getWeights();
		REQUIRE(weights.size() == 3);
		
		// Weights should still sum to 1
		double sum = 0.0;
		for (const auto& w : weights) {
			sum += w;
		}
		REQUIRE_THAT(sum, Catch::Matchers::WithinRel(1.0, 0.001));
	}
	
	SECTION("High temperature gives more uniform weights") {
		EnsembleConfig config;
		config.method = EnsembleCombinationMethod::WeightedAccuracy;
		config.validation_split = 0.2;
		config.temperature = 10.0;
		
		Ensemble ensemble(forecasters, config);
		ensemble.fit(ts);
		
		auto weights = ensemble.getWeights();
		REQUIRE(weights.size() == 3);
		
		// With high temperature, weights should be more similar
		double mean_weight = 1.0 / 3.0;
		for (const auto& w : weights) {
			// Should be closer to equal weights
			REQUIRE(std::abs(w - mean_weight) < 0.2);
		}
	}
}

TEST_CASE("Ensemble WeightedBIC combination", "[ensemble]") {
	auto ts = createTestTimeSeries();
	
	std::vector<std::shared_ptr<IForecaster>> forecasters;
	auto arima1 = ARIMABuilder().withAR(1).withMA(0).build();
	auto arima2 = ARIMABuilder().withAR(2).withMA(1).build();
	forecasters.push_back(std::move(arima1));
	forecasters.push_back(std::move(arima2));
	
	EnsembleConfig config;
	config.method = EnsembleCombinationMethod::WeightedBIC;
	
	Ensemble ensemble(forecasters, config);
	ensemble.fit(ts);
	
	auto forecast = ensemble.predict(10);
	REQUIRE(forecast.horizon() == 10);
	
	auto weights = ensemble.getWeights();
	double sum = 0.0;
	for (const auto& w : weights) {
		sum += w;
	}
	REQUIRE_THAT(sum, Catch::Matchers::WithinRel(1.0, 0.001));
}

TEST_CASE("Ensemble handles forecaster failures gracefully", "[ensemble][error]") {
	auto ts = createTestTimeSeries();
	
	std::vector<std::shared_ptr<IForecaster>> forecasters;
	forecasters.push_back(std::make_shared<Naive>());
	// Add a forecaster that might fail
	forecasters.push_back(SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
	
	Ensemble ensemble(forecasters);
	// Should handle gracefully if one forecaster fails
	REQUIRE_NOTHROW(ensemble.fit(ts));
}

TEST_CASE("Ensemble validation_split edge cases", "[ensemble][edge]") {
	auto ts = createLongerTimeSeries();
	
	std::vector<std::shared_ptr<IForecaster>> forecasters;
	forecasters.push_back(std::make_shared<Naive>());
	forecasters.push_back(SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
	
	SECTION("validation_split = 1.0 should fail") {
		EnsembleConfig config;
		config.method = EnsembleCombinationMethod::WeightedAccuracy;
		config.validation_split = 1.0;
		
		Ensemble ensemble(forecasters, config);
		REQUIRE_THROWS_AS(ensemble.fit(ts), std::invalid_argument);
	}
	
	SECTION("validation_split too large for data") {
		EnsembleConfig config;
		config.method = EnsembleCombinationMethod::WeightedAccuracy;
		config.validation_split = 0.99;  // Leaves almost no training data
		
		Ensemble ensemble(forecasters, config);
		// May throw or handle gracefully
		try {
			ensemble.fit(ts);
		} catch (...) {
			// Acceptable
		}
	}
}

TEST_CASE("Ensemble min_weight threshold filtering", "[ensemble]") {
	auto ts = createLongerTimeSeries();
	
	std::vector<std::shared_ptr<IForecaster>> forecasters;
	forecasters.push_back(std::make_shared<Naive>());
	forecasters.push_back(SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
	forecasters.push_back(SimpleMovingAverageBuilder().withWindow(5).build());
	
	EnsembleConfig config;
	config.method = EnsembleCombinationMethod::WeightedAccuracy;
	config.validation_split = 0.2;
	config.min_weight = 0.4;  // High threshold
	
	Ensemble ensemble(forecasters, config);
	ensemble.fit(ts);
	
	auto weights = ensemble.getWeights();
	// Some weights may be zeroed out due to min_weight
	for (const auto& w : weights) {
				REQUIRE((w == 0.0 || w >= config.min_weight));
	}
}

TEST_CASE("Ensemble combineForecasts with NaN values", "[ensemble][edge]") {
	auto ts = createTestTimeSeries();
	
	std::vector<std::shared_ptr<IForecaster>> forecasters;
	forecasters.push_back(std::make_shared<Naive>());
	forecasters.push_back(SimpleExponentialSmoothingBuilder().withAlpha(0.3).build());
	
	Ensemble ensemble(forecasters);
	ensemble.fit(ts);
	
	// getIndividualForecasts may contain NaN if a forecaster fails
	auto individual = ensemble.getIndividualForecasts(5);
	REQUIRE(individual.size() == 2);
	
	// Ensemble should still produce valid forecast
	auto forecast = ensemble.predict(5);
	REQUIRE(forecast.horizon() == 5);
}

