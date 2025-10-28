#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/core/forecast.hpp"

#include <stdexcept>
#include <vector>

using anofoxtime::core::Forecast;

TEST_CASE("Forecast lazily expands dimensions and series", "[core][forecast]") {
	Forecast forecast;

	auto &primary = forecast.series();
	primary = {1.0, 2.0, 3.0};

	auto &secondary = forecast.series(1);
	secondary = {10.0, 11.0, 12.0};

	REQUIRE(forecast.dimensions() == 2);
	REQUIRE(forecast.isMultivariate());
	REQUIRE_FALSE(forecast.empty());
	REQUIRE(forecast.horizon() == 3);

	const auto &primary_const = static_cast<const Forecast &>(forecast).primary();
	REQUIRE(primary_const.size() == 3);
	REQUIRE(primary_const[1] == Catch::Approx(2.0));

	REQUIRE(forecast.series(5).empty());
	REQUIRE(forecast.dimensions() == 6);
}

TEST_CASE("Forecast manages prediction intervals", "[core][forecast][intervals]") {
	Forecast forecast;
	forecast.series() = {1.0, 2.0, 3.0};

	auto &lower = forecast.lowerSeries();
	lower = {0.5, 1.5, 2.5};

	auto &upper = forecast.upperSeries();
	upper = {1.5, 2.5, 3.5};

	REQUIRE(forecast.lower.has_value());
	REQUIRE(forecast.upper.has_value());

	const auto &lower_const = static_cast<const Forecast &>(forecast).lowerSeries();
	const auto &upper_const = static_cast<const Forecast &>(forecast).upperSeries();

	REQUIRE(lower_const[0] == Catch::Approx(0.5));
	REQUIRE(upper_const[2] == Catch::Approx(3.5));

	REQUIRE_THROWS_AS(static_cast<const Forecast &>(forecast).lowerSeries(1), std::out_of_range);
	REQUIRE_THROWS_AS(static_cast<const Forecast &>(forecast).upperSeries(1), std::out_of_range);
}

TEST_CASE("Forecast empty state reflects missing values", "[core][forecast][empty]") {
	Forecast forecast;
	REQUIRE(forecast.empty());
	REQUIRE(forecast.horizon() == 0);

	forecast.series(); // creates dimension but leaves it empty
	REQUIRE(forecast.empty());

	forecast.primary().push_back(1.0);
	REQUIRE_FALSE(forecast.empty());
	REQUIRE(forecast.horizon() == 1);
}
