#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/models/arima.hpp"
#include "common/time_series_helpers.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

using anofoxtime::models::ARIMABuilder;

namespace {

std::vector<double> generateARSeries(double phi, double start, std::size_t length) {
	std::vector<double> series;
	series.reserve(length);
	series.push_back(start);
	for (std::size_t i = 1; i < length; ++i) {
		series.push_back(phi * series.back());
	}
	return series;
}

} // namespace

TEST_CASE("ARIMA builder enforces valid orders", "[models][arima][builder]") {
	REQUIRE_THROWS_AS(ARIMABuilder().build(), std::invalid_argument);

	ARIMABuilder invalid_ar;
	invalid_ar.withAR(-1);
	REQUIRE_THROWS_AS(invalid_ar.build(), std::invalid_argument);

	ARIMABuilder valid;
	valid.withAR(1);
	REQUIRE_NOTHROW(valid.build());
}

TEST_CASE("ARIMA fit estimates AR(1) coefficient", "[models][arima][fit]") {
	const double phi = 0.8;
	const auto data = generateARSeries(phi, 1.0, 40);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	auto model = ARIMABuilder().withAR(1).withDifferencing(0).withMA(0).withIntercept(false).build();
	model->fit(ts);

	REQUIRE(model->arCoefficients().size() == 1);
	REQUIRE(model->arCoefficients()[0] == Catch::Approx(phi).margin(0.1));

	const auto forecast = model->predict(3);
	REQUIRE(forecast.primary().size() == 3);

	double expected = data.back();
	for (double value : forecast.primary()) {
		expected *= phi;
		REQUIRE(value == Catch::Approx(expected).margin(0.15));
	}
}

TEST_CASE("ARIMA rejects multivariate input", "[models][arima][validation]") {
	auto multivariate = tests::helpers::makeMultivariateByColumns({{1.0, 0.8, 0.6, 0.5}, {0.5, 0.4, 0.3, 0.2}});
	auto model = ARIMABuilder().withAR(1).withDifferencing(0).withMA(0).withIntercept(false).build();
	REQUIRE_THROWS_AS(model->fit(multivariate), std::invalid_argument);
}

TEST_CASE("ARIMA confidence intervals bracket forecast", "[models][arima][forecast]") {
	const auto data = generateARSeries(0.5, 2.0, 30);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	auto model = ARIMABuilder().withAR(1).withMA(0).withDifferencing(0).withIntercept(true).build();
	model->fit(ts);

	constexpr int horizon = 2;
	auto forecast = model->predictWithConfidence(horizon, 0.9);
	REQUIRE(forecast.primary().size() == horizon);
	REQUIRE(forecast.lowerSeries().size() == horizon);
	REQUIRE(forecast.upperSeries().size() == horizon);

	for (std::size_t i = 0; i < static_cast<std::size_t>(horizon); ++i) {
		REQUIRE(forecast.lowerSeries()[i] <= forecast.primary()[i]);
		REQUIRE(forecast.upperSeries()[i] >= forecast.primary()[i]);
	}
}
