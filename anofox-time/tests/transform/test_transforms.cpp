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

// Pipeline error cases and edge cases
TEST_CASE("Pipeline cannot add transformer after fitting") {
	Pipeline pipeline;
	auto scaler1 = std::make_unique<MinMaxScaler>();
	pipeline.addTransformer(std::move(scaler1));
	
	std::vector<double> data{1.0, 2.0, 3.0};
	pipeline.fitTransform(data);
	
	auto scaler2 = std::make_unique<MinMaxScaler>();
	REQUIRE_THROWS_AS(pipeline.addTransformer(std::move(scaler2)), std::runtime_error);
}

TEST_CASE("Pipeline transform requires fitting") {
	Pipeline pipeline;
	auto scaler = std::make_unique<MinMaxScaler>();
	pipeline.addTransformer(std::move(scaler));
	
	std::vector<double> data{1.0, 2.0, 3.0};
	REQUIRE_THROWS_AS(pipeline.transform(data), std::runtime_error);
}

TEST_CASE("Pipeline inverseTransform requires fitting") {
	Pipeline pipeline;
	auto scaler = std::make_unique<MinMaxScaler>();
	pipeline.addTransformer(std::move(scaler));
	
	std::vector<double> data{1.0, 2.0, 3.0};
	REQUIRE_THROWS_AS(pipeline.inverseTransform(data), std::runtime_error);
}

TEST_CASE("Pipeline inverseTransformForecast requires fitting") {
	Pipeline pipeline;
	auto scaler = std::make_unique<MinMaxScaler>();
	pipeline.addTransformer(std::move(scaler));
	
	Forecast forecast;
	forecast.point = {{1.0, 2.0, 3.0}};
	REQUIRE_THROWS_AS(pipeline.inverseTransformForecast(forecast), std::runtime_error);
}

TEST_CASE("Pipeline with empty forecast does nothing") {
	Pipeline pipeline;
	auto scaler = std::make_unique<MinMaxScaler>();
	pipeline.addTransformer(std::move(scaler));
	
	std::vector<double> data{1.0, 2.0, 3.0};
	pipeline.fitTransform(data);
	
	Forecast forecast;
	forecast.point = {};
	REQUIRE_NOTHROW(pipeline.inverseTransformForecast(forecast));
	REQUIRE(forecast.point.empty());
}

TEST_CASE("Pipeline with multiple transformers") {
	std::vector<double> original{1.0, 2.0, 3.0, 4.0};
	std::vector<double> transformed = original;

	Pipeline pipeline;
	pipeline.addTransformer(std::make_unique<MinMaxScaler>());
	pipeline.addTransformer(std::make_unique<StandardScaler>());
	pipeline.fitTransform(transformed);
	
	REQUIRE(pipeline.isFitted());
	REQUIRE(pipeline.size() == 2);
	
	Forecast forecast;
	forecast.point = {transformed};
	pipeline.inverseTransformForecast(forecast);
	
	expectSeriesEqual(forecast.point.front(), original, 1e-5);
}

TEST_CASE("Pipeline constructed with transformers") {
	std::vector<std::unique_ptr<Transformer>> transformers;
	transformers.push_back(std::make_unique<MinMaxScaler>());
	
	Pipeline pipeline(std::move(transformers));
	REQUIRE(pipeline.size() == 1);
	
	std::vector<double> data{1.0, 2.0, 3.0};
	pipeline.fitTransform(data);
	REQUIRE(pipeline.isFitted());
}

TEST_CASE("Pipeline fit and transform separately") {
	Pipeline pipeline;
	pipeline.addTransformer(std::make_unique<MinMaxScaler>());
	
	std::vector<double> data{1.0, 2.0, 3.0};
	pipeline.fit(data);
	REQUIRE(pipeline.isFitted());
	
	pipeline.transform(data);
	expectSeriesEqual(data, {0.0, 0.5, 1.0});
}

// Log transformer tests
TEST_CASE("Log transform and inverse") {
	std::vector<double> data{1.0, 2.0, 3.0};
	Log log_transform;
	log_transform.fit(data);
	log_transform.transform(data);
	
	REQUIRE(data[0] == 0.0); // log(1) = 0
	REQUIRE(approxEqual(data[1], std::log(2.0)));
	REQUIRE(approxEqual(data[2], std::log(3.0)));
	
	log_transform.inverseTransform(data);
	expectSeriesEqual(data, {1.0, 2.0, 3.0});
}

// Additional edge cases for existing transformers
TEST_CASE("LinearInterpolator with edge NaNs") {
	std::vector<double> data{std::numeric_limits<double>::quiet_NaN(), 2.0, 3.0};
	LinearInterpolator interpolator;
	interpolator.transform(data);
	// Edge NaNs may not be interpolated
	REQUIRE(!std::isnan(data[1]));
	REQUIRE(!std::isnan(data[2]));
}

TEST_CASE("MinMaxScaler with custom range") {
	std::vector<double> data{1.0, 2.0, 3.0};
	MinMaxScaler scaler;
	scaler.withScaledRange(-1.0, 1.0);
	scaler.fitTransform(data);
	
	expectSeriesEqual(data, {-1.0, 0.0, 1.0});
}

TEST_CASE("StandardScaler with parameters") {
	std::vector<double> data{1.0, 2.0, 3.0};
	StandardScaler scaler;
	auto params = StandardScaleParams::fromData(data);
	scaler.withParameters(params);
	scaler.transform(data);
	
	// Should be standardized
	REQUIRE(approxEqual(data[0], -1.224744871391589, 1e-6));
	REQUIRE(approxEqual(data[1], 0.0, 1e-6));
	REQUIRE(approxEqual(data[2], 1.224744871391589, 1e-6));
}
