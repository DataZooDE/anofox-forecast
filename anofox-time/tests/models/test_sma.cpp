#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/models/sma.hpp"
#include "common/time_series_helpers.hpp"

#include <stdexcept>
#include <numeric>
#include <vector>

using anofoxtime::models::SimpleMovingAverageBuilder;

namespace {

std::vector<double> expectedSmaForecast(const std::vector<double> &history, std::size_t window, int horizon) {
	std::vector<double> temp = history;
	std::vector<double> forecast;
	forecast.reserve(horizon);
	for (int i = 0; i < horizon; ++i) {
		const double sum = std::accumulate(temp.end() - window, temp.end(), 0.0);
		const double next = sum / static_cast<double>(window);
		forecast.push_back(next);
		temp.push_back(next);
	}
	return forecast;
}

} // namespace

TEST_CASE("SMA builder validates window", "[models][sma][builder]") {
	SimpleMovingAverageBuilder builder;
	
	// window=0 is now valid (means use full history)
	REQUIRE_NOTHROW(builder.withWindow(0).build());
	
	// Negative window should still be rejected
	REQUIRE_THROWS_AS(builder.withWindow(-1).build(), std::invalid_argument);

	auto model = SimpleMovingAverageBuilder().withWindow(3).build();
	REQUIRE(model->getName() == "SimpleMovingAverage");
}

TEST_CASE("SMA requires sufficient history", "[models][sma]") {
	auto ts = tests::helpers::makeUnivariateSeries({1.0, 2.0});
	auto model = SimpleMovingAverageBuilder().withWindow(3).build();
	REQUIRE_THROWS_AS(model->fit(ts), std::invalid_argument);
	REQUIRE_THROWS_AS(model->predict(1), std::runtime_error); // still unfitted
}

TEST_CASE("SMA rejects multivariate input", "[models][sma][validation]") {
	auto multivariate = tests::helpers::makeMultivariateByColumns({{1.0, 2.0, 3.0}, {4.0, 5.0, 6.0}});
	auto model = SimpleMovingAverageBuilder().withWindow(2).build();
	REQUIRE_THROWS_AS(model->fit(multivariate), std::invalid_argument);
}

TEST_CASE("SMA forecasts repeating averages", "[models][sma][forecast]") {
	const std::vector<double> history{1.0, 2.0, 3.0, 4.0, 5.0};
	auto ts = tests::helpers::makeUnivariateSeries(history);

	auto model = SimpleMovingAverageBuilder().withWindow(3).build();
	model->fit(ts);

	constexpr int horizon = 3;
	const auto forecast = model->predict(horizon);

	REQUIRE(forecast.primary().size() == horizon);

	const auto expected = expectedSmaForecast(history, 3, horizon);
	for (int i = 0; i < horizon; ++i) {
		REQUIRE(forecast.primary()[i] == Catch::Approx(expected[static_cast<std::size_t>(i)]).margin(1e-6));
	}
}

TEST_CASE("SMA handles zero horizon", "[models][sma][forecast]") {
	auto ts = tests::helpers::makeUnivariateSeries({10.0, 11.0, 12.0, 13.0, 14.0});
	auto model = SimpleMovingAverageBuilder().withWindow(2).build();
	model->fit(ts);

	const auto forecast = model->predict(0);
	REQUIRE(forecast.empty());
	REQUIRE(forecast.horizon() == 0);
}
