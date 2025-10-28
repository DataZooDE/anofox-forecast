#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/seasonality/detector.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using anofoxtime::seasonality::Periodogram;
using anofoxtime::seasonality::SeasonalityDetector;

namespace {

std::vector<double> makeSineWave(std::size_t length, std::size_t period) {
	std::vector<double> data(length);
	for (std::size_t i = 0; i < length; ++i) {
		const double angle = 2.0 * M_PI * static_cast<double>(i) / static_cast<double>(period);
		data[i] = std::sin(angle);
	}
	return data;
}

} // namespace

TEST_CASE("Seasonality detector finds dominant period", "[seasonality][detector]") {
	const std::size_t period = 12;
	const auto data = makeSineWave(period * 6, period);
	auto detector = SeasonalityDetector::builder().minPeriod(2).threshold(0.6).build();
	const auto periods = detector.detect(data, 5);
	REQUIRE_FALSE(periods.empty());
	REQUIRE(std::find(periods.begin(), periods.end(), period) != periods.end());
}

TEST_CASE("Periodogram peaks obey threshold", "[seasonality][detector][periodogram]") {
	Periodogram pg;
	pg.periods = {2, 3, 4, 5};
	pg.powers = {0.1, 0.4, 0.8, 0.2};

	const auto peaks = pg.peaks(0.5);
	REQUIRE(peaks.size() == 1);
	REQUIRE(peaks.front().period == 4);
	REQUIRE(peaks.front().power == Catch::Approx(0.8));
}
