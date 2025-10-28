#include <catch2/catch_test_macros.hpp>

#include "anofox-time/core/forecast.hpp"
#include "anofox-time/transform/transformers.hpp"

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using anofoxtime::core::Forecast;
using namespace anofoxtime::transform;

namespace {

bool approxEqual(double lhs, double rhs, double eps = 1e-6) {
	if (std::isnan(lhs) && std::isnan(rhs)) {
		return true;
	}
	return std::fabs(lhs - rhs) <= eps;
}

void expectSeriesEqual(const std::vector<double> &lhs, const std::vector<double> &rhs, double eps = 1e-6) {
	REQUIRE(lhs.size() == rhs.size());
	for (std::size_t i = 0; i < lhs.size(); ++i) {
		REQUIRE(approxEqual(lhs[i], rhs[i], eps));
	}
}

} // namespace

TEST_CASE("LinearInterpolator fills interior NaNs") {
	std::vector<double> data{1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(), 4.0};
	LinearInterpolator interpolator;
	interpolator.transform(data);

	expectSeriesEqual(data, {1.0, 2.0, 3.0, 4.0});
}

TEST_CASE("BoxCox manual lambda") {
	std::vector<double> data{1.0, 2.0, 3.0};
	BoxCox box_cox;
	box_cox.withLambda(0.5);
	box_cox.transform(data);
	expectSeriesEqual(data, {0.0, 0.8284271247461903, 1.4641016151377544});
	box_cox.inverseTransform(data);
	expectSeriesEqual(data, {1.0, 2.0, 3.0});
}

TEST_CASE("BoxCox fit ignores NaNs when requested") {
	std::vector<double> data{1.0, 2.0, std::numeric_limits<double>::quiet_NaN(), 3.0};
	std::vector<double> expected{1.0, 2.0, 3.0};

	BoxCox reference;
	reference.ignoreNaNs(true);
	reference.fitTransform(expected);

	BoxCox box_cox;
	box_cox.ignoreNaNs(true);
	box_cox.fitTransform(data);
	REQUIRE(std::isnan(data[2]));
	CAPTURE(data[0], data[1], data[3]);
	expectSeriesEqual({data[0], data[1], data[3]}, expected);
}

TEST_CASE("YeoJohnson transforms with manual lambda") {
	std::vector<double> data{-1.0, 0.0, 1.0};
	YeoJohnson transform;
	transform.withLambda(0.5);
	transform.transform(data);
	expectSeriesEqual(data, {-1.2189514164974602, 0.0, 0.8284271247461903});
	transform.inverseTransform(data);
	expectSeriesEqual(data, {-1.0, 0.0, 1.0});
}

TEST_CASE("MinMaxScaler scales and preserves NaNs") {
	std::vector<double> data{1.0, std::numeric_limits<double>::quiet_NaN(), 2.0, 3.0};
	MinMaxScaler scaler;
	scaler.fitTransform(data);
	REQUIRE(std::isnan(data[1]));
	expectSeriesEqual({data[0], data[2], data[3]}, {0.0, 0.5, 1.0});

	scaler.inverseTransform(data);
	expectSeriesEqual({data[0], data[2], data[3]}, {1.0, 2.0, 3.0});
	REQUIRE(std::isnan(data[1]));
}

TEST_CASE("StandardScaler with ignore NaNs") {
	std::vector<double> data{1.0, std::numeric_limits<double>::quiet_NaN(), 2.0, 3.0};
	StandardScaler scaler;
	scaler.ignoreNaNs(true);
	scaler.fitTransform(data);

	REQUIRE(std::isnan(data[1]));
	expectSeriesEqual({data[0], data[2], data[3]},
	                  {-1.224744871391589, 0.0, 1.224744871391589}, 1e-6);
}

TEST_CASE("Logit transform and inverse") {
	std::vector<double> data{0.5, 0.75, 0.25};
	Logit logit;
	logit.transform(data);
	expectSeriesEqual(data,
	                  {0.0, std::log(0.75 / (1.0 - 0.75)), std::log(0.25 / (1.0 - 0.25))});

	logit.inverseTransform(data);
	expectSeriesEqual(data, {0.5, 0.75, 0.25});
}

TEST_CASE("Pipeline inverse restores forecast data") {
	std::vector<double> original{1.0, 2.0, 3.0};
	std::vector<double> transformed = original;

	Pipeline pipeline;
	auto scaler = std::make_unique<MinMaxScaler>();
	pipeline.addTransformer(std::move(scaler));
	pipeline.fitTransform(transformed);

	Forecast forecast;
	forecast.point = {transformed};
	pipeline.inverseTransformForecast(forecast);

	expectSeriesEqual(forecast.point.front(), original);
}
