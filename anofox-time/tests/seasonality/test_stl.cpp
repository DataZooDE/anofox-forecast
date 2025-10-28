#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/seasonality/stl.hpp"
#include "common/time_series_helpers.hpp"

#include <cmath>
#include <vector>

using anofoxtime::seasonality::STLDecomposition;

namespace {

std::vector<double> buildTrendSeasonSeries(std::size_t length, std::size_t period) {
	std::vector<double> data(length);
	for (std::size_t i = 0; i < length; ++i) {
		const double angle = 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(period);
		const double seasonal = std::sin(angle);
		const double trend = 0.05 * static_cast<double>(i);
		data[i] = trend + seasonal;
	}
	return data;
}

} // namespace

TEST_CASE("STL decomposition extracts seasonal strength", "[seasonality][stl]") {
	const std::size_t period = 12;
	const auto data = buildTrendSeasonSeries(period * 6, period);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	auto stl = STLDecomposition::builder()
	                 .withPeriod(period)
	                 .withSeasonalSmoother(period)
	                 .withTrendSmoother(period * 2 + 1)
	                 .withIterations(2)
	                 .withRobust(false)
	                 .build();
	stl.fit(ts);

	REQUIRE(stl.seasonalStrength() > 0.7);
	REQUIRE(stl.trendStrength() > 0.2);
}

TEST_CASE("STL requires sufficient history", "[seasonality][stl][error]") {
	auto ts = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0});
	auto stl = STLDecomposition::builder().withPeriod(4).withSeasonalSmoother(5).withTrendSmoother(7).build();
	REQUIRE_THROWS_AS(stl.fit(ts), std::invalid_argument);
}
