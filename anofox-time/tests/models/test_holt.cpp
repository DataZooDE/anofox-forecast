#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/models/holt.hpp"
#include "common/time_series_helpers.hpp"

#include <numeric>
#include <stdexcept>
#include <vector>

using anofoxtime::models::HoltLinearTrendBuilder;

TEST_CASE("Holt builder enforces smoothing bounds", "[models][holt][builder]") {
	REQUIRE_THROWS_AS(HoltLinearTrendBuilder().withAlpha(-0.1).build(), std::invalid_argument);
	REQUIRE_THROWS_AS(HoltLinearTrendBuilder().withBeta(1.5).build(), std::invalid_argument);

	auto model = HoltLinearTrendBuilder().withAlpha(0.5).withBeta(0.3).build();
	REQUIRE(model->getName() == "HoltLinearTrend");
}

TEST_CASE("Holt fit requires at least two points", "[models][holt]") {
	auto model = HoltLinearTrendBuilder().withAlpha(0.5).withBeta(0.3).build();
	auto ts = tests::helpers::makeUnivariateSeries({42.0});
	REQUIRE_THROWS_AS(model->fit(ts), std::invalid_argument);
	REQUIRE_THROWS_AS(model->predict(1), std::runtime_error);
}

TEST_CASE("Holt rejects multivariate input", "[models][holt][validation]") {
	auto model = HoltLinearTrendBuilder().withAlpha(0.5).withBeta(0.3).build();
	auto multivariate = tests::helpers::makeMultivariateByColumns({{1.0, 2.0, 3.0}, {2.0, 3.5, 4.0}});
	REQUIRE_THROWS_AS(model->fit(multivariate), std::invalid_argument);
}

TEST_CASE("Holt forecasts extrapolate linear trend", "[models][holt][forecast]") {
	const std::vector<double> history{2.0, 4.0, 6.0, 8.0};
	auto ts = tests::helpers::makeUnivariateSeries(history);

	auto model = HoltLinearTrendBuilder().withAlpha(0.8).withBeta(0.2).build();
	model->fit(ts);

	constexpr int horizon = 3;
	const auto forecast = model->predict(horizon);
	REQUIRE(forecast.primary().size() == horizon);

	// For perfectly linear data, we expect the continuation of the slope (~2 per step)
	for (int step = 1; step <= horizon; ++step) {
		const double expected = history.back() + step * 2.0;
		REQUIRE(forecast.primary()[step - 1] == Catch::Approx(expected).margin(0.25));
	}
}

TEST_CASE("Holt reject negative horizons", "[models][holt][forecast]") {
	auto model = HoltLinearTrendBuilder().withAlpha(0.5).withBeta(0.5).build();
	auto ts = tests::helpers::makeUnivariateSeries({1.0, 2.0});
	model->fit(ts);
	REQUIRE_THROWS_AS(model->predict(-1), std::invalid_argument);
}
