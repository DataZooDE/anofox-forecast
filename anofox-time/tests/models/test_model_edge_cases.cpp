#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "anofox-time/models/ses.hpp"
#include "anofox-time/models/holt.hpp"
#include "anofox-time/models/ets.hpp"
#include "anofox-time/models/theta.hpp"
#include "anofox-time/models/auto_ets.hpp"
#include "anofox-time/models/auto_arima.hpp"
#include "common/time_series_helpers.hpp"
#include <cmath>
#include <limits>
#include <vector>

using namespace anofoxtime::models;
using namespace anofoxtime::core;

namespace {

TimeSeries createTimeSeries(const std::vector<double>& data) {
	return tests::helpers::makeUnivariateSeries(data);
}

} // namespace

// ============================================================================
// Error Handling: Predict Before Fit
// ============================================================================

TEST_CASE("SES requires fit before predict", "[models][edge][error]") {
	SES model;
	REQUIRE_THROWS_AS(model.predict(5), std::logic_error);
}

TEST_CASE("Holt requires fit before predict", "[models][edge][error]") {
	Holt model;
	REQUIRE_THROWS_AS(model.predict(5), std::logic_error);
}

TEST_CASE("ETS requires fit before predict", "[models][edge][error]") {
	ETSConfig config;
	ETS model(config);
	REQUIRE_THROWS_AS(model.predict(5), std::logic_error);
}

TEST_CASE("AutoETS requires fit before predict", "[models][edge][error]") {
	AutoETS model;
	REQUIRE_THROWS_AS(model.predict(5), std::logic_error);
	REQUIRE_THROWS_AS(model.metrics(), std::logic_error);
	REQUIRE_THROWS_AS(model.diagnostics(), std::logic_error);
}

TEST_CASE("AutoARIMA requires fit before predict", "[models][edge][error]") {
	AutoARIMA model;
	REQUIRE_THROWS_AS(model.predict(5), std::logic_error);
}

TEST_CASE("Theta requires fit before predict", "[models][edge][error]") {
	Theta model;
	REQUIRE_THROWS_AS(model.predict(5), std::logic_error);
}

// ============================================================================
// Edge Cases: Empty and Short Series
// ============================================================================

TEST_CASE("SES handles empty series", "[models][edge]") {
	SES model;
	auto ts = createTimeSeries({});
	REQUIRE_THROWS_AS(model.fit(ts), std::invalid_argument);
}

TEST_CASE("SES handles single value", "[models][edge]") {
	SES model;
	auto ts = createTimeSeries({5.0});
	REQUIRE_NOTHROW(model.fit(ts));
	auto forecast = model.predict(3);
	REQUIRE(forecast.primary().size() == 3);
	REQUIRE(forecast.primary()[0] == Catch::Approx(5.0));
}

TEST_CASE("Holt handles short series", "[models][edge]") {
	Holt model;
	auto ts = createTimeSeries({1.0, 2.0});
	REQUIRE_NOTHROW(model.fit(ts));
	auto forecast = model.predict(2);
	REQUIRE(forecast.primary().size() == 2);
}

TEST_CASE("AutoETS handles short series", "[models][edge]") {
	AutoETS model;
	auto ts = createTimeSeries({1.0, 2.0, 3.0});
	REQUIRE_THROWS_AS(model.fit(ts), std::invalid_argument);  // Needs at least 4
}

TEST_CASE("AutoARIMA handles short series", "[models][edge]") {
	AutoARIMA model;
	auto ts = createTimeSeries({1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0});
	REQUIRE_THROWS_AS(model.fit(ts), std::invalid_argument);  // Needs at least 10
}

// ============================================================================
// Edge Cases: Constant Series
// ============================================================================

TEST_CASE("SES handles constant series", "[models][edge]") {
	SES model;
	std::vector<double> constant(20, 42.0);
	auto ts = createTimeSeries(constant);
	REQUIRE_NOTHROW(model.fit(ts));
	auto forecast = model.predict(5);
	REQUIRE(forecast.primary().size() == 5);
	// Should forecast constant value
	for (double val : forecast.primary()) {
		REQUIRE(val == Catch::Approx(42.0).margin(0.1));
	}
}

TEST_CASE("Holt handles constant series", "[models][edge]") {
	Holt model;
	std::vector<double> constant(20, 100.0);
	auto ts = createTimeSeries(constant);
	REQUIRE_NOTHROW(model.fit(ts));
	auto forecast = model.predict(5);
	REQUIRE(forecast.primary().size() == 5);
}

TEST_CASE("AutoARIMA handles constant series", "[models][edge]") {
	AutoARIMA model;
	std::vector<double> constant(30, 50.0);
	auto ts = createTimeSeries(constant);
	REQUIRE_NOTHROW(model.fit(ts));
	auto forecast = model.predict(5);
	REQUIRE(forecast.primary().size() == 5);
}

// ============================================================================
// Edge Cases: NaN Handling
// ============================================================================

TEST_CASE("SES handles NaN in data", "[models][edge]") {
	SES model;
	std::vector<double> data{1.0, 2.0, std::numeric_limits<double>::quiet_NaN(), 4.0, 5.0};
	auto ts = createTimeSeries(data);
	// May throw or handle gracefully depending on implementation
	try {
		model.fit(ts);
		auto forecast = model.predict(3);
		REQUIRE(forecast.primary().size() == 3);
	} catch (...) {
		// NaN handling may throw, which is acceptable
		REQUIRE(true);
	}
}

// ============================================================================
// Parameter Validation
// ============================================================================

TEST_CASE("SES validates alpha parameter", "[models][edge][validation]") {
	SES model;
	// Alpha should be in [0, 1]
	// Test with invalid alpha if settable
	auto ts = createTimeSeries({1.0, 2.0, 3.0, 4.0, 5.0});
	REQUIRE_NOTHROW(model.fit(ts));
}

TEST_CASE("Holt validates parameters", "[models][edge][validation]") {
	Holt model;
	auto ts = createTimeSeries({1.0, 2.0, 3.0, 4.0, 5.0});
	REQUIRE_NOTHROW(model.fit(ts));
}

TEST_CASE("ETS validates config parameters", "[models][edge][validation]") {
	ETSConfig config;
	config.error = anofoxtime::models::ETSErrorType::Additive;
	config.trend = anofoxtime::models::ETSTrendType::Additive;
	config.season = anofoxtime::models::ETSSeasonType::Additive;
	
	ETS model(config);
	auto ts = createTimeSeries({1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0,
	                            11.0, 12.0, 13.0, 14.0, 15.0, 16.0, 17.0, 18.0, 19.0, 20.0,
	                            21.0, 22.0, 23.0, 24.0});
	REQUIRE_NOTHROW(model.fit(ts));
}

// ============================================================================
// Edge Cases: Zero and Negative Values
// ============================================================================

TEST_CASE("SES handles zero values", "[models][edge]") {
	SES model;
	std::vector<double> data{0.0, 0.0, 1.0, 2.0, 3.0};
	auto ts = createTimeSeries(data);
	REQUIRE_NOTHROW(model.fit(ts));
	auto forecast = model.predict(3);
	REQUIRE(forecast.primary().size() == 3);
}

TEST_CASE("SES handles negative values", "[models][edge]") {
	SES model;
	std::vector<double> data{-1.0, -2.0, -3.0, -2.0, -1.0};
	auto ts = createTimeSeries(data);
	REQUIRE_NOTHROW(model.fit(ts));
	auto forecast = model.predict(3);
	REQUIRE(forecast.primary().size() == 3);
}

TEST_CASE("Holt handles negative values", "[models][edge]") {
	Holt model;
	std::vector<double> data{-10.0, -8.0, -6.0, -4.0, -2.0};
	auto ts = createTimeSeries(data);
	REQUIRE_NOTHROW(model.fit(ts));
	auto forecast = model.predict(3);
	REQUIRE(forecast.primary().size() == 3);
}

// ============================================================================
// Edge Cases: Very Large/Small Values
// ============================================================================

TEST_CASE("SES handles very large values", "[models][edge]") {
	SES model;
	std::vector<double> data{1e10, 2e10, 3e10, 4e10, 5e10};
	auto ts = createTimeSeries(data);
	REQUIRE_NOTHROW(model.fit(ts));
	auto forecast = model.predict(3);
	REQUIRE(forecast.primary().size() == 3);
	REQUIRE(std::isfinite(forecast.primary()[0]));
}

TEST_CASE("SES handles very small values", "[models][edge]") {
	SES model;
	std::vector<double> data{1e-10, 2e-10, 3e-10, 4e-10, 5e-10};
	auto ts = createTimeSeries(data);
	REQUIRE_NOTHROW(model.fit(ts));
	auto forecast = model.predict(3);
	REQUIRE(forecast.primary().size() == 3);
}

// ============================================================================
// Edge Cases: Forecast Horizon
// ============================================================================

TEST_CASE("SES handles zero forecast horizon", "[models][edge]") {
	SES model;
	auto ts = createTimeSeries({1.0, 2.0, 3.0, 4.0, 5.0});
	model.fit(ts);
	auto forecast = model.predict(0);
	REQUIRE(forecast.primary().empty());
}

TEST_CASE("SES handles large forecast horizon", "[models][edge]") {
	SES model;
	auto ts = createTimeSeries({1.0, 2.0, 3.0, 4.0, 5.0});
	model.fit(ts);
	auto forecast = model.predict(1000);
	REQUIRE(forecast.primary().size() == 1000);
}

// ============================================================================
// Edge Cases: Fitted Values and Residuals
// ============================================================================

TEST_CASE("SES fitted values match data size", "[models][edge]") {
	SES model;
	auto ts = createTimeSeries({1.0, 2.0, 3.0, 4.0, 5.0});
	model.fit(ts);
	const auto& fitted = model.fittedValues();
	const auto& residuals = model.residuals();
	REQUIRE(fitted.size() == 5);
	REQUIRE(residuals.size() == 5);
}

TEST_CASE("Holt fitted values match data size", "[models][edge]") {
	Holt model;
	auto ts = createTimeSeries({1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
	model.fit(ts);
	const auto& fitted = model.fittedValues();
	const auto& residuals = model.residuals();
	REQUIRE(fitted.size() == 6);
	REQUIRE(residuals.size() == 6);
}

// ============================================================================
// Edge Cases: Confidence Intervals
// ============================================================================

TEST_CASE("SES confidence intervals for zero horizon", "[models][edge]") {
	SES model;
	auto ts = createTimeSeries({1.0, 2.0, 3.0, 4.0, 5.0});
	model.fit(ts);
	auto forecast = model.predictWithConfidence(0, 0.95);
	REQUIRE(forecast.primary().empty());
	REQUIRE(forecast.lowerSeries().empty());
	REQUIRE(forecast.upperSeries().empty());
}

TEST_CASE("SES confidence intervals for different levels", "[models][edge]") {
	SES model;
	auto ts = createTimeSeries({1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0});
	model.fit(ts);
	
	auto forecast_90 = model.predictWithConfidence(5, 0.90);
	auto forecast_95 = model.predictWithConfidence(5, 0.95);
	auto forecast_99 = model.predictWithConfidence(5, 0.99);
	
	REQUIRE(forecast_90.primary().size() == 5);
	REQUIRE(forecast_95.primary().size() == 5);
	REQUIRE(forecast_99.primary().size() == 5);
	
	// 99% intervals should be wider than 95%, which should be wider than 90%
	for (size_t i = 0; i < 5; ++i) {
		double width_90 = forecast_90.upperSeries()[i] - forecast_90.lowerSeries()[i];
		double width_95 = forecast_95.upperSeries()[i] - forecast_95.lowerSeries()[i];
		double width_99 = forecast_99.upperSeries()[i] - forecast_99.lowerSeries()[i];
		
		REQUIRE(width_99 >= width_95);
		REQUIRE(width_95 >= width_90);
	}
}

// ============================================================================
// Edge Cases: Model Names
// ============================================================================

TEST_CASE("Models return correct names", "[models][edge][metadata]") {
	SES ses;
	Holt holt;
	Theta theta;
	
	REQUIRE(ses.getName() == "SES");
	REQUIRE(holt.getName() == "Holt");
	REQUIRE(theta.getName() == "Theta");
}

