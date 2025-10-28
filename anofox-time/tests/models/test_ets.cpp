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

    // Mirror initializeStates logic for trend-only configuration
    double level = data.front();
    double trend;
    {
        double numerator = 0.0;
        std::size_t count = 0;
        const std::size_t m = 1;
        for (std::size_t i = 0; i + m < data.size(); ++i) {
            numerator += (data[i + m] - data[i]) / static_cast<double>(m);
            ++count;
        }
        trend = (count > 0) ? numerator / static_cast<double>(count) : (data[1] - data[0]);
    }

    for (std::size_t t = 0; t < data.size(); ++t) {
        double base = level;
        base += trend;

        const double fitted = base;
        const double error = data[t] - fitted;

        const double new_level = base + config.alpha * error;
        const double new_trend = trend + config.beta.value() * error;

        level = new_level;
        trend = new_trend;
    }

    const double expected1 = level + trend;
    const double expected2 = level + 2.0 * trend;
    REQUIRE(forecast.primary()[0] == Catch::Approx(expected1).margin(1e-6));
    REQUIRE(forecast.primary()[1] == Catch::Approx(expected2).margin(1e-6));
}
