#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/seasonality/mstl.hpp"
#include "common/time_series_helpers.hpp"

#include <cmath>
#include <numeric>
#include <vector>

using anofoxtime::seasonality::MSTLDecomposition;

namespace {

std::vector<double> buildMultiSeasonSeries(std::size_t length) {
	std::vector<double> data(length);
	for (std::size_t i = 0; i < length; ++i) {
		double seasonal7 = std::sin(2.0 * M_PI * static_cast<double>(i) / 7.0);
		double seasonal12 = 0.5 * std::sin(2.0 * M_PI * static_cast<double>(i) / 12.0);
		double trend = 0.02 * static_cast<double>(i);
		data[i] = trend + seasonal7 + seasonal12;
	}
	return data;
}

double rms(const std::vector<double>& values) {
	if (values.empty()) return 0.0;
	double sum_sq = 0.0;
	for (double v : values) {
		sum_sq += v * v;
	}
	return std::sqrt(sum_sq / static_cast<double>(values.size()));
}

} // namespace

TEST_CASE("MSTL decomposition handles multiple seasonalities", "[seasonality][mstl]") {
	const auto data = buildMultiSeasonSeries(140);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	auto mstl = MSTLDecomposition::builder()
	                 .withPeriods({7, 12})
	                 .withIterations(2)
	                 .withRobust(false)
	                 .build();

	mstl.fit(ts);
	const auto& components = mstl.components();
	REQUIRE(components.seasonal.size() == 2);
	REQUIRE(components.trend.size() == data.size());
	REQUIRE(components.remainder.size() == data.size());
	for (const auto& seasonal : components.seasonal) {
		REQUIRE(seasonal.size() == data.size());
	}
	REQUIRE(rms(components.remainder) < 0.3);
}

TEST_CASE("MSTL requires valid periods", "[seasonality][mstl][error]") {
	REQUIRE_THROWS_AS(MSTLDecomposition::builder().withPeriods({1}).build(), std::invalid_argument);
}
