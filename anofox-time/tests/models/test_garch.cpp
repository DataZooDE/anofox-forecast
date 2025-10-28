#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/models/garch.hpp"

#include <numeric>
#include <stdexcept>
#include <vector>

using anofoxtime::models::GARCH;

namespace {

std::vector<double> constantSeries(double value, std::size_t length) {
	return std::vector<double>(length, value);
}

double computeExpectedForecast(double omega, double alpha, double beta, std::size_t history_length, int horizon) {
	const auto data = constantSeries(5.0, history_length);

	const double mean = 5.0;
	std::vector<double> residuals(history_length, 0.0);
	std::vector<double> sigma2(history_length, 0.0);

	const std::size_t max_order = 1;
	std::fill(sigma2.begin(), sigma2.begin() + max_order, 0.0);

	for (std::size_t t = 0; t < history_length; ++t) {
		residuals[t] = data[t] - mean;
		if (t < max_order)
			continue;
		sigma2[t] = omega + alpha * residuals[t - 1] * residuals[t - 1] + beta * sigma2[t - 1];
	}

	double variance = sigma2.back();
	double arch_sum = alpha;
	double garch_sum = beta;
	double unconditional = omega / (1.0 - arch_sum - garch_sum);

	for (int h = 0; h < horizon; ++h) {
		variance = omega + (arch_sum + garch_sum) * variance;
	}

	return variance + unconditional;
}

} // namespace

TEST_CASE("GARCH validates parameters", "[models][garch][config]") {
	REQUIRE_THROWS_AS(GARCH(0, 1, 0.1, {0.1}, {0.2}), std::invalid_argument);
	REQUIRE_THROWS_AS(GARCH(1, 1, -0.1, {0.1}, {0.2}), std::invalid_argument);
	REQUIRE_THROWS_AS(GARCH(1, 1, 0.1, {-0.1}, {0.2}), std::invalid_argument);
	REQUIRE_THROWS_AS(GARCH(1, 1, 0.1, {0.8}, {0.3}), std::invalid_argument); // sum >= 1

	REQUIRE_NOTHROW(GARCH(1, 1, 0.1, {0.2}, {0.5}));
}

TEST_CASE("GARCH forecasting follows recursion", "[models][garch][forecast]") {
	GARCH model(1, 1, 0.1, {0.2}, {0.5});
	const auto data = constantSeries(5.0, 30);
	model.fit(data);

	const int horizon = 2;
	const double forecast = model.forecastVariance(horizon);

	const double expected = computeExpectedForecast(0.1, 0.2, 0.5, data.size(), horizon);
	REQUIRE(forecast == Catch::Approx(expected).margin(1e-6));
}

TEST_CASE("GARCH forecast requires prior fit", "[models][garch][error]") {
	GARCH model(1, 1, 0.1, {0.2}, {0.5});
	REQUIRE_THROWS_AS(model.forecastVariance(1), std::runtime_error);

	const auto data = constantSeries(4.0, 10);
	model.fit(data);
	REQUIRE_THROWS_AS(model.forecastVariance(0), std::invalid_argument);
}
