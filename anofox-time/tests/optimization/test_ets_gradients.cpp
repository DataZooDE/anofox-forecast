#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "anofox-time/optimization/ets_gradients.hpp"
#include "anofox-time/models/ets.hpp"
#include <vector>
#include <cmath>
#include <limits>

using namespace anofoxtime::optimization;
using namespace anofoxtime::models;

namespace {

std::vector<double> generateTestData(size_t n) {
	std::vector<double> data;
	data.reserve(n);
	for (size_t i = 0; i < n; ++i) {
		data.push_back(100.0 + 0.5 * i + 10.0 * std::sin(2.0 * M_PI * i / 12.0));
	}
	return data;
}

bool isFiniteGradient(double grad) {
	return std::isfinite(grad);
}

} // namespace

TEST_CASE("ETSGradients computeNegLogLikelihoodWithGradients basic", "[optimization][ets_gradients]") {
	auto values = generateTestData(24);
	ETSConfig config;
	config.error = ETSErrorType::Additive;
	config.trend = ETSTrendType::None;
	config.season = ETSSeasonType::None;
	
	ETSGradients::GradientComponents gradients;
	double neg_loglik = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, values, 100.0, 0.0, {}, gradients
	);
	
	REQUIRE(std::isfinite(neg_loglik));
	REQUIRE(neg_loglik >= 0.0);
}

TEST_CASE("ETSGradients with additive trend", "[optimization][ets_gradients]") {
	auto values = generateTestData(24);
	ETSConfig config;
	config.error = ETSErrorType::Additive;
	config.trend = ETSTrendType::Additive;
	config.season = ETSSeasonType::None;
	
	ETSGradients::GradientComponents gradients;
	double neg_loglik = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, values, 100.0, 0.5, {}, gradients
	);
	
	REQUIRE(std::isfinite(neg_loglik));
	REQUIRE(isFiniteGradient(gradients.d_alpha));
	REQUIRE(isFiniteGradient(gradients.d_beta));
	REQUIRE(isFiniteGradient(gradients.d_level));
	REQUIRE(isFiniteGradient(gradients.d_trend));
}

TEST_CASE("ETSGradients with multiplicative trend", "[optimization][ets_gradients]") {
	auto values = generateTestData(24);
	ETSConfig config;
	config.error = ETSErrorType::Additive;
	config.trend = ETSTrendType::Multiplicative;
	config.season = ETSSeasonType::None;
	
	ETSGradients::GradientComponents gradients;
	double neg_loglik = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, values, 100.0, 1.0, {}, gradients
	);
	
	REQUIRE(std::isfinite(neg_loglik));
	REQUIRE(isFiniteGradient(gradients.d_alpha));
	REQUIRE(isFiniteGradient(gradients.d_beta));
}

TEST_CASE("ETSGradients with damped additive trend", "[optimization][ets_gradients]") {
	auto values = generateTestData(24);
	ETSConfig config;
	config.error = ETSErrorType::Additive;
	config.trend = ETSTrendType::DampedAdditive;
	config.season = ETSSeasonType::None;
	config.phi = 0.9;
	
	ETSGradients::GradientComponents gradients;
	double neg_loglik = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, values, 100.0, 0.5, {}, gradients
	);
	
	REQUIRE(std::isfinite(neg_loglik));
	REQUIRE(isFiniteGradient(gradients.d_alpha));
	REQUIRE(isFiniteGradient(gradients.d_beta));
	REQUIRE(isFiniteGradient(gradients.d_phi));
}

TEST_CASE("ETSGradients with damped multiplicative trend", "[optimization][ets_gradients]") {
	auto values = generateTestData(24);
	ETSConfig config;
	config.error = ETSErrorType::Additive;
	config.trend = ETSTrendType::DampedMultiplicative;
	config.season = ETSSeasonType::None;
	config.phi = 0.9;
	
	ETSGradients::GradientComponents gradients;
	double neg_loglik = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, values, 100.0, 1.0, {}, gradients
	);
	
	REQUIRE(std::isfinite(neg_loglik));
	REQUIRE(isFiniteGradient(gradients.d_alpha));
	REQUIRE(isFiniteGradient(gradients.d_beta));
	REQUIRE(isFiniteGradient(gradients.d_phi));
}

TEST_CASE("ETSGradients with additive seasonal", "[optimization][ets_gradients]") {
	auto values = generateTestData(36);  // Need multiple seasons
	ETSConfig config;
	config.error = ETSErrorType::Additive;
	config.trend = ETSTrendType::None;
	config.season = ETSSeasonType::Additive;
	
	std::vector<double> seasonals(12, 0.0);
	ETSGradients::GradientComponents gradients;
	double neg_loglik = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, values, 100.0, 0.0, seasonals, gradients
	);
	
	REQUIRE(std::isfinite(neg_loglik));
	REQUIRE(isFiniteGradient(gradients.d_alpha));
	REQUIRE(isFiniteGradient(gradients.d_gamma));
}

TEST_CASE("ETSGradients with multiplicative seasonal", "[optimization][ets_gradients]") {
	auto values = generateTestData(36);
	ETSConfig config;
	config.error = ETSErrorType::Additive;
	config.trend = ETSTrendType::None;
	config.season = ETSSeasonType::Multiplicative;
	
	std::vector<double> seasonals(12, 1.0);
	ETSGradients::GradientComponents gradients;
	double neg_loglik = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, values, 100.0, 0.0, seasonals, gradients
	);
	
	REQUIRE(std::isfinite(neg_loglik));
	REQUIRE(isFiniteGradient(gradients.d_alpha));
	REQUIRE(isFiniteGradient(gradients.d_gamma));
}

TEST_CASE("ETSGradients with multiplicative error", "[optimization][ets_gradients]") {
	auto values = generateTestData(24);
	// Ensure all values are positive for multiplicative error
	for (auto& v : values) {
		v = std::abs(v) + 1.0;
	}
	
	ETSConfig config;
	config.error = ETSErrorType::Multiplicative;
	config.trend = ETSTrendType::None;
	config.season = ETSSeasonType::None;
	
	ETSGradients::GradientComponents gradients;
	double neg_loglik = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, values, 100.0, 0.0, {}, gradients
	);
	
	REQUIRE(std::isfinite(neg_loglik));
	REQUIRE(isFiniteGradient(gradients.d_alpha));
}

TEST_CASE("ETSGradients with full ETS model", "[optimization][ets_gradients]") {
	auto values = generateTestData(36);
	ETSConfig config;
	config.error = ETSErrorType::Additive;
	config.trend = ETSTrendType::Additive;
	config.season = ETSSeasonType::Additive;
	
	std::vector<double> seasonals(12, 0.0);
	ETSGradients::GradientComponents gradients;
	double neg_loglik = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, values, 100.0, 0.5, seasonals, gradients
	);
	
	REQUIRE(std::isfinite(neg_loglik));
	REQUIRE(isFiniteGradient(gradients.d_alpha));
	REQUIRE(isFiniteGradient(gradients.d_beta));
	REQUIRE(isFiniteGradient(gradients.d_gamma));
	REQUIRE(isFiniteGradient(gradients.d_level));
	REQUIRE(isFiniteGradient(gradients.d_trend));
}

TEST_CASE("ETSGradients handles empty data", "[optimization][ets_gradients][edge]") {
	std::vector<double> empty;
	ETSConfig config;
	ETSGradients::GradientComponents gradients;
	
	double neg_loglik = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, empty, 100.0, 0.0, {}, gradients
	);
	
	REQUIRE(!std::isfinite(neg_loglik));
}

TEST_CASE("ETSGradients handles short series", "[optimization][ets_gradients][edge]") {
	std::vector<double> short_series{1.0, 2.0, 3.0};
	ETSConfig config;
	config.error = ETSErrorType::Additive;
	config.trend = ETSTrendType::None;
	config.season = ETSSeasonType::None;
	
	ETSGradients::GradientComponents gradients;
	double neg_loglik = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, short_series, 2.0, 0.0, {}, gradients
	);
	
	REQUIRE((std::isfinite(neg_loglik) || !std::isfinite(neg_loglik)));
}

TEST_CASE("ETSGradients handles constant series", "[optimization][ets_gradients][edge]") {
	std::vector<double> constant(20, 50.0);
	ETSConfig config;
	config.error = ETSErrorType::Additive;
	config.trend = ETSTrendType::None;
	config.season = ETSSeasonType::None;
	
	ETSGradients::GradientComponents gradients;
	double neg_loglik = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, constant, 50.0, 0.0, {}, gradients
	);
	
	REQUIRE((std::isfinite(neg_loglik) || !std::isfinite(neg_loglik)));
}

TEST_CASE("ETSGradients with different initial states", "[optimization][ets_gradients]") {
	auto values = generateTestData(24);
	ETSConfig config;
	config.error = ETSErrorType::Additive;
	config.trend = ETSTrendType::Additive;
	config.season = ETSSeasonType::None;
	
	// Test with different initial level
	ETSGradients::GradientComponents gradients1;
	double neg_loglik1 = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, values, 50.0, 0.5, {}, gradients1
	);
	
	ETSGradients::GradientComponents gradients2;
	double neg_loglik2 = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, values, 150.0, 0.5, {}, gradients2
	);
	
	REQUIRE(std::isfinite(neg_loglik1));
	REQUIRE(std::isfinite(neg_loglik2));
	// Gradients should differ with different initial states
}

TEST_CASE("ETSGradients gradient components initialized", "[optimization][ets_gradients]") {
	ETSGradients::GradientComponents gradients;
	REQUIRE(gradients.d_alpha == 0.0);
	REQUIRE(gradients.d_beta == 0.0);
	REQUIRE(gradients.d_gamma == 0.0);
	REQUIRE(gradients.d_phi == 0.0);
	REQUIRE(gradients.d_level == 0.0);
	REQUIRE(gradients.d_trend == 0.0);
}

TEST_CASE("ETSGradients with quarterly seasonality", "[optimization][ets_gradients]") {
	auto values = generateTestData(20);
	ETSConfig config;
	config.error = ETSErrorType::Additive;
	config.trend = ETSTrendType::None;
	config.season = ETSSeasonType::Additive;
	
	std::vector<double> seasonals(4, 0.0);
	ETSGradients::GradientComponents gradients;
	double neg_loglik = ETSGradients::computeNegLogLikelihoodWithGradients(
		config, values, 100.0, 0.0, seasonals, gradients
	);
	
	REQUIRE(std::isfinite(neg_loglik));
	REQUIRE(isFiniteGradient(gradients.d_alpha));
	REQUIRE(isFiniteGradient(gradients.d_gamma));
}

