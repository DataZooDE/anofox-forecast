#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/models/ses.hpp"
#include "common/time_series_helpers.hpp"

#include <numeric>
#include <stdexcept>
#include <vector>

using anofoxtime::models::SimpleExponentialSmoothingBuilder;

TEST_CASE("SES builder enforces alpha bounds", "[models][ses][builder]") {
	REQUIRE_THROWS_AS(SimpleExponentialSmoothingBuilder().withAlpha(-0.1).build(), std::invalid_argument);
	REQUIRE_THROWS_AS(SimpleExponentialSmoothingBuilder().withAlpha(1.5).build(), std::invalid_argument);

	auto model = SimpleExponentialSmoothingBuilder().withAlpha(0.3).build();
	REQUIRE(model->getName() == "SimpleExponentialSmoothing");
}

TEST_CASE("SES fit requires data", "[models][ses]") {
	auto model = SimpleExponentialSmoothingBuilder().withAlpha(0.5).build();
	auto empty_series = tests::helpers::makeUnivariateSeries({});
	REQUIRE_THROWS_AS(model->fit(empty_series), std::invalid_argument);
	REQUIRE_THROWS_AS(model->predict(1), std::runtime_error);
}

TEST_CASE("SES rejects multivariate input", "[models][ses][validation]") {
	auto multivariate = tests::helpers::makeMultivariateByColumns({{1.0, 2.0, 3.0}, {2.0, 3.0, 4.0}});
	auto model = SimpleExponentialSmoothingBuilder().withAlpha(0.5).build();
	REQUIRE_THROWS_AS(model->fit(multivariate), std::invalid_argument);
}

TEST_CASE("SES forecasts converge to final level", "[models][ses][forecast]") {
	const std::vector<double> history{10.0, 12.0, 11.0, 13.0};
	auto ts = tests::helpers::makeUnivariateSeries(history);

	auto model = SimpleExponentialSmoothingBuilder().withAlpha(0.5).build();
	model->fit(ts);

	constexpr int horizon = 4;
	const auto forecast = model->predict(horizon);
	REQUIRE(forecast.primary().size() == horizon);

	// Compute expected level iteratively
	double level = history.front();
	for (std::size_t i = 1; i < history.size(); ++i) {
		level = 0.5 * history[i] + 0.5 * level;
	}
	for (double value : forecast.primary()) {
		REQUIRE(value == Catch::Approx(level).margin(1e-6));
	}
}

TEST_CASE("SES returns empty forecast for zero horizon", "[models][ses][forecast]") {
	auto model = SimpleExponentialSmoothingBuilder().withAlpha(0.4).build();
	auto ts = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0});
	model->fit(ts);
	const auto forecast = model->predict(0);
	REQUIRE(forecast.empty());
}
