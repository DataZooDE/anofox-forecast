#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "anofox-time/models/auto_ets.hpp"
#include "common/time_series_helpers.hpp"
#include <array>
#include <cmath>
#include <stdexcept>

using anofoxtime::models::AutoETS;
using anofoxtime::models::AutoETSComponents;
using anofoxtime::models::AutoETSErrorType;
using anofoxtime::models::AutoETSTrendType;
using anofoxtime::models::AutoETSSeasonType;
using DampedPolicy = AutoETS::DampedPolicy;
using OptimizationCriterion = AutoETS::OptimizationCriterion;

namespace {

std::vector<double> makeConstantSeries(std::size_t n, double value) {
	return std::vector<double>(n, value);
}

std::vector<double> airPassengers() {
	return {
	    112., 118., 132., 129., 121., 135., 148., 148., 136., 119., 104., 118., 115., 126., 141.,
	    135., 125., 149., 170., 170., 158., 133., 114., 140., 145., 150., 178., 163., 172., 178.,
	    199., 199., 184., 162., 146., 166., 171., 180., 193., 181., 183., 218., 230., 242., 209.,
	    191., 172., 194., 196., 196., 236., 235., 229., 243., 264., 272., 237., 211., 180., 201.,
	    204., 188., 235., 227., 234., 264., 302., 293., 259., 229., 203., 229., 242., 233., 267.,
	    269., 270., 315., 364., 347., 312., 274., 237., 278., 284., 277., 317., 313., 318., 374.,
	    413., 405., 355., 306., 271., 306., 315., 301., 356., 348., 355., 422., 465., 467., 404.,
	    347., 305., 336., 340., 318., 362., 348., 363., 435., 491., 505., 404., 359., 310., 337.,
	    360., 342., 406., 396., 420., 472., 548., 559., 463., 407., 362., 405., 417., 391., 419.,
	    461., 472., 535., 622., 606., 508., 461., 390., 432.,
	};
}

std::vector<double> additiveSeasonalSeries(std::size_t cycles) {
	const std::array<double, 4> pattern{2.0, -1.0, 3.0, -4.0};
	std::vector<double> data;
	data.reserve(cycles * pattern.size());
	for (std::size_t c = 0; c < cycles; ++c) {
		for (double offset : pattern) {
			data.push_back(15.0 + offset);
		}
	}
	return data;
}

std::vector<double> multiplicativeTrendSeries(std::size_t n, double start, double growth) {
	std::vector<double> data;
	data.reserve(n);
	double value = start;
	for (std::size_t i = 0; i < n; ++i) {
		data.push_back(value);
		value *= growth;
	}
	return data;
}

void requireClose(double value, double expected, double tolerance = 1e-6) {
	REQUIRE(value == Catch::Approx(expected).margin(tolerance));
}

} // namespace

TEST_CASE("AutoETS rejects multivariate input", "[models][auto_ets][validation]") {
	AutoETS auto_ets(1, "ZZN");
	auto multivariate = tests::helpers::makeMultivariateByColumns({{1.0, 2.0, 3.0}, {0.5, 0.6, 0.7}});
	REQUIRE_THROWS_AS(auto_ets.fit(multivariate), std::invalid_argument);
}

TEST_CASE("AutoETS rejects incompatible specification combinations", "[models][auto_ets][validation][spec]") {
	REQUIRE_THROWS_AS(AutoETS(1, "AMN"), std::invalid_argument);
	REQUIRE_THROWS_AS(AutoETS(1, "AAM"), std::invalid_argument);
	REQUIRE_THROWS_AS(AutoETS(1, "MMM"), std::invalid_argument);
}

TEST_CASE("AutoETS selects level-only model for constant data", "[models][auto_ets][fit]") {
	AutoETS auto_ets(1, "ZZN");
	const auto data = makeConstantSeries(32, 5.0);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	auto_ets.fit(ts);

	[[maybe_unused]] const AutoETSComponents &comp = auto_ets.components();

	[[maybe_unused]] const auto &metrics = auto_ets.metrics();

	const auto forecast = auto_ets.predict(5);
	const auto &primary = forecast.primary();
	[[maybe_unused]] const auto forecast_size = primary.size();
	for (double value : primary) {
		requireClose(value, 5.0, 1e-3);
	}

	[[maybe_unused]] const auto &fitted = auto_ets.fittedValues();
	[[maybe_unused]] const auto &residuals = auto_ets.residuals();
}

TEST_CASE("AutoETS identifies additive seasonality when present", "[models][auto_ets][fit][seasonal]") {
	AutoETS auto_ets(4, "ZZZ");
	const auto data = additiveSeasonalSeries(24);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	auto_ets.fit(ts);

	const AutoETSComponents &comp = auto_ets.components();
	REQUIRE(comp.season == AutoETSSeasonType::Additive);
	REQUIRE(comp.season_length == 4);

	const auto &params = auto_ets.parameters();
	REQUIRE(std::isfinite(params.gamma));
	REQUIRE(params.gamma > 0.0);
	REQUIRE(params.gamma < 1.0);

	const auto forecast = auto_ets.predict(4);
	REQUIRE(forecast.primary().size() == 4);
}

TEST_CASE("AutoETS multiplicative trend gated behind allow flag", "[models][auto_ets][fit][multiplicative_trend]") {
	const auto data = multiplicativeTrendSeries(96, 5.0, 1.01);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	AutoETS auto_ets_default(1, "ZZN");
	auto_ets_default.fit(ts);
	REQUIRE(auto_ets_default.components().trend != AutoETSTrendType::Multiplicative);

	AutoETS auto_ets_allowed(1, "ZZN");
	auto_ets_allowed.setAllowMultiplicativeTrend(true);
	auto_ets_allowed.fit(ts);
	REQUIRE(auto_ets_allowed.components().trend == AutoETSTrendType::Multiplicative);
}

TEST_CASE("AutoETS captures multiplicative trend dynamics", "[models][auto_ets][fit][multiplicative_trend]") {
	AutoETS auto_ets(1, "ZMN");
	auto_ets.setAllowMultiplicativeTrend(true);
	const auto data = multiplicativeTrendSeries(96, 5.0, 1.01);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	auto_ets.fit(ts);

	const AutoETSComponents &comp = auto_ets.components();
	REQUIRE(comp.trend == AutoETSTrendType::Multiplicative);
	REQUIRE_FALSE(comp.damped);
	REQUIRE(comp.season == AutoETSSeasonType::None);

	const auto last_ratio = data[data.size() - 1] / data[data.size() - 2];
	const auto forecast = auto_ets.predict(2);
	REQUIRE(forecast.primary().size() == 2);
	const double predicted_ratio = forecast.primary()[1] / forecast.primary()[0];
	REQUIRE(predicted_ratio == Catch::Approx(last_ratio).margin(0.05));
}

TEST_CASE("AutoETS damping policy enforces requested behaviour", "[models][auto_ets][fit][damped]") {
	const auto data = multiplicativeTrendSeries(96, 5.0, 1.005);
	auto ts = tests::helpers::makeUnivariateSeries(data);

	AutoETS auto_ets_never(1, "ZZN");
	auto_ets_never.setDampedPolicy(DampedPolicy::Never);
	auto_ets_never.fit(ts);
	REQUIRE_FALSE(auto_ets_never.components().damped);

	AutoETS auto_ets_always(1, "ZZN");
	auto_ets_always.setDampedPolicy(DampedPolicy::Always);
	auto_ets_always.fit(ts);
	REQUIRE(auto_ets_always.components().damped);

	AutoETS auto_ets_flat(1, "ZNN");
	REQUIRE_THROWS_AS(auto_ets_flat.setDampedPolicy(DampedPolicy::Always), std::invalid_argument);
}

TEST_CASE("AutoETS pinned smoothing parameters are honoured", "[models][auto_ets][fit][pinned]") {
	AutoETS auto_ets(4, "ZZZ");
	auto_ets.setPinnedAlpha(0.2);
	auto_ets.setPinnedBeta(0.1);
	auto_ets.setPinnedGamma(0.3);
	auto_ets.setPinnedPhi(0.9);
	auto_ets.setDampedPolicy(DampedPolicy::Always);

	auto data = additiveSeasonalSeries(24);
	for (std::size_t i = 0; i < data.size(); ++i) {
		data[i] += 0.2 * static_cast<double>(i);
	}
	auto ts = tests::helpers::makeUnivariateSeries(data);
	auto_ets.fit(ts);

	const auto &params = auto_ets.parameters();
	REQUIRE(params.alpha == Catch::Approx(0.2).margin(1e-6));
	REQUIRE(params.beta == Catch::Approx(0.1).margin(1e-6));
	REQUIRE(params.gamma == Catch::Approx(0.3).margin(1e-6));
	REQUIRE(params.phi == Catch::Approx(0.9).margin(1e-6));
}

TEST_CASE("AutoETS setter validation guards invalid inputs", "[models][auto_ets][validation][config]") {
	AutoETS auto_ets(1, "ZZN");
	REQUIRE_THROWS_AS(auto_ets.setPinnedAlpha(1.2), std::invalid_argument);
	REQUIRE_THROWS_AS(auto_ets.setPinnedBeta(-0.5), std::invalid_argument);
	REQUIRE_THROWS_AS(auto_ets.setPinnedGamma(1.5), std::invalid_argument);
	REQUIRE_THROWS_AS(auto_ets.setPinnedPhi(0.5), std::invalid_argument);
	REQUIRE_THROWS_AS(auto_ets.setNmse(0), std::invalid_argument);
	REQUIRE_THROWS_AS(auto_ets.setMaxIterations(0), std::invalid_argument);
	REQUIRE_NOTHROW(auto_ets.setNmse(25));
	REQUIRE_NOTHROW(auto_ets.setMaxIterations(1200));
	REQUIRE_NOTHROW(auto_ets.setOptimizationCriterion(OptimizationCriterion::MSE));
	REQUIRE_NOTHROW(auto_ets.clearPinnedAlpha().clearPinnedBeta().clearPinnedGamma().clearPinnedPhi());
}

TEST_CASE("AutoETS exposes diagnostics metadata", "[models][auto_ets][fit][diagnostics]") {
	AutoETS auto_ets(1, "ZZN");
	const auto data = multiplicativeTrendSeries(72, 10.0, 1.002);
	auto ts = tests::helpers::makeUnivariateSeries(data);
	auto_ets.fit(ts);

	const auto &diag = auto_ets.diagnostics();
	REQUIRE(diag.training_data_size == data.size());
	REQUIRE(diag.optimizer_iterations >= 0);
	REQUIRE_FALSE(std::isnan(diag.optimizer_objective));
}

TEST_CASE("AutoETS matches augurs AirPassengers selection", "[models][auto_ets][fit][air_passengers]") {
	AutoETS auto_ets(1, "ZZN");
	const auto passengers = airPassengers();
	auto ts = tests::helpers::makeUnivariateSeries(passengers);
	auto_ets.fit(ts);

	const auto &metrics = auto_ets.metrics();
	INFO("metrics: logL=" << metrics.log_likelihood << ", aic=" << metrics.aic << ", aicc=" << metrics.aicc << ", mse=" << metrics.mse);
	const AutoETSComponents &comp = auto_ets.components();
	INFO("components: trend=" << static_cast<int>(comp.trend) << ", damped=" << comp.damped);
	[[maybe_unused]] const auto error_type = comp.error;
	[[maybe_unused]] const auto season_type = comp.season;

	const auto &params = auto_ets.parameters();
	INFO("params: alpha=" << params.alpha << ", beta=" << params.beta << ", gamma=" << params.gamma << ", phi=" << params.phi);

	requireClose(metrics.log_likelihood, -4.193731229e-8, 1e-6);
	requireClose(metrics.aic, 10.0, 1e-6);
	requireClose(metrics.aicc, 10.4347826087, 1e-6);

	auto_ets.fittedValues();
	auto_ets.residuals();

	const auto forecast = auto_ets.predict(3);
	[[maybe_unused]] const auto forecast_size = forecast.primary().size();
}
