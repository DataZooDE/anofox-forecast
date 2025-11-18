#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/models/ets.hpp"
#include "common/time_series_helpers.hpp"

#include <stdexcept>
#include <vector>

using anofoxtime::models::ETS;
using anofoxtime::models::ETSConfig;
using anofoxtime::models::ETSSeasonType;
using anofoxtime::models::ETSTrendType;
using anofoxtime::models::ETSErrorType;

TEST_CASE("ETS configuration validation", "[models][ets][config]") {
	ETSConfig config;
	config.alpha = 0.4;
	config.trend = ETSTrendType::Additive;
	// Missing beta should trigger validation failure
	REQUIRE_THROWS_AS(ETS(config), std::invalid_argument);

	config.beta = 0.2;
	config.season = ETSSeasonType::Additive;
	config.season_length = 4;
	// Missing gamma when seasonality enabled
	REQUIRE_THROWS_AS(ETS(config), std::invalid_argument);

	config.gamma = 0.1;
	config.error = ETSErrorType::Additive;
	REQUIRE_NOTHROW(ETS(config));
}

TEST_CASE("ETS rejects multivariate input", "[models][ets][validation]") {
	ETSConfig config;
	config.alpha = 0.5;
	config.trend = ETSTrendType::None;
	config.season = ETSSeasonType::None;

	ETS model(config);
	auto multivariate = tests::helpers::makeMultivariateByColumns({{1.0, 2.0, 3.0}, {0.5, 0.6, 0.7}});
	REQUIRE_THROWS_AS(model.fit(multivariate), std::invalid_argument);
}

TEST_CASE("ETS forecasts constant series", "[models][ets][forecast]") {
	ETSConfig config;
	config.alpha = 0.8;
	config.trend = ETSTrendType::None;
	config.season = ETSSeasonType::None;

	ETS model(config);
	auto ts = tests::helpers::makeUnivariateSeries({5.0, 5.0, 5.0, 5.0});
	model.fit(ts);

	constexpr int horizon = 3;
	const auto forecast = model.predict(horizon);
	REQUIRE(forecast.primary().size() == horizon);
	for (double value : forecast.primary()) {
		REQUIRE(value == Catch::Approx(5.0).margin(1e-6));
	}
}

TEST_CASE("ETS additive trend extrapolates", "[models][ets][trend]") {
    ETSConfig config;
    config.alpha = 0.5;
    config.trend = ETSTrendType::Additive;
    config.beta = 0.4;
    config.season = ETSSeasonType::None;

    const std::vector<double> data{3.0, 5.0, 7.0, 9.0};
    ETS model(config);
    auto ts = tests::helpers::makeUnivariateSeries(data);
    model.fit(ts);

    const auto forecast = model.predict(2);
    REQUIRE(forecast.primary().size() == 2);

    // Test that forecast shows increasing trend (using statsforecast implementation)
    // The actual values come from statsforecast's ETS implementation which uses
    // a different update formula than the simplified manual calculation
    REQUIRE(forecast.primary()[0] > data.back());
    REQUIRE(forecast.primary()[1] > forecast.primary()[0]);
    
    // Verify reasonable values (should be around 10-12 for first step, 11-13 for second)
    REQUIRE(forecast.primary()[0] > 9.0);
    REQUIRE(forecast.primary()[0] < 15.0);
    REQUIRE(forecast.primary()[1] > forecast.primary()[0]);
    REQUIRE(forecast.primary()[1] < 20.0);
}
