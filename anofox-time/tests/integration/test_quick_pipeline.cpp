#include <catch2/catch_test_macros.hpp>

#include "anofox-time/quick.hpp"
#include "anofox-time/transform/transformers.hpp"
#include "anofox-time/validation.hpp"
#include "common/metrics_helpers.hpp"

#include <cstddef>
#include <cmath>
#include <memory>
#include <vector>

using anofoxtime::validation::accuracyMetrics;

namespace {

const std::vector<double> &airPassengersSeries() {
	static const std::vector<double> data{
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
	return data;
}

} // namespace

TEST_CASE("Quick moving average summary matches validation", "[integration][quick]") {
	std::vector<double> train{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	std::vector<double> actual{11, 12, 13};
	std::vector<double> baseline{10, 11, 12};

	const auto summary = anofoxtime::quick::movingAverage(train, 3, static_cast<int>(actual.size()), actual, baseline);
	REQUIRE(summary.metrics.has_value());

	const auto expected = accuracyMetrics(actual, summary.forecast.series(), baseline);
	tests::helpers::expectAccuracyApprox(*summary.metrics, expected, 1e-6);

	SECTION("handles seasonal airline passengers series") {
		const auto &passengers = airPassengersSeries();
		const std::size_t horizon = 12;
		REQUIRE(passengers.size() > horizon);

		std::vector<double> history(passengers.begin(), passengers.end() - static_cast<std::ptrdiff_t>(horizon));
		std::vector<double> holdout(passengers.end() - static_cast<std::ptrdiff_t>(horizon), passengers.end());
		std::vector<double> holdout_baseline(horizon, history.back());

		const auto seasonal_summary = anofoxtime::quick::movingAverage(
		    history, 12, static_cast<int>(horizon), holdout, holdout_baseline);
		REQUIRE(seasonal_summary.metrics.has_value());

		const auto seasonal_expected =
		    accuracyMetrics(holdout, seasonal_summary.forecast.series(), holdout_baseline);
		tests::helpers::expectAccuracyApprox(*seasonal_summary.metrics, seasonal_expected, 1e-6);
	}
}

TEST_CASE("Quick ARIMA forecast produces metrics", "[integration][quick][arima]") {
	std::vector<double> train;
	for (int i = 1; i <= 30; ++i) {
		train.push_back(static_cast<double>(i));
	}
	std::vector<double> actual{31, 32, 33};

	const auto summary = anofoxtime::quick::arima(train, 1, 1, 0, static_cast<int>(actual.size()), actual);
	REQUIRE(summary.metrics.has_value());
	REQUIRE(summary.forecast.horizon() == actual.size());
	REQUIRE(summary.metrics->rmse >= 0.0);

	SECTION("handles seasonal airline passengers series") {
		const auto &passengers = airPassengersSeries();
		const std::size_t horizon = 12;
		REQUIRE(passengers.size() > horizon);

		std::vector<double> history(passengers.begin(), passengers.end() - static_cast<std::ptrdiff_t>(horizon));
		std::vector<double> holdout(passengers.end() - static_cast<std::ptrdiff_t>(horizon), passengers.end());
		std::vector<double> holdout_baseline(horizon, history.back());

		const auto seasonal_summary = anofoxtime::quick::arima(
		    history, 1, 1, 1, static_cast<int>(horizon), holdout, holdout_baseline);
		REQUIRE(seasonal_summary.metrics.has_value());
		REQUIRE(seasonal_summary.forecast.horizon() == horizon);

		const auto seasonal_expected =
		    accuracyMetrics(holdout, seasonal_summary.forecast.series(), holdout_baseline);
		tests::helpers::expectAccuracyApprox(*seasonal_summary.metrics, seasonal_expected, 1e-6);

		if (seasonal_summary.aic.has_value()) {
			REQUIRE(std::isfinite(*seasonal_summary.aic));
		}
		if (seasonal_summary.bic.has_value()) {
			REQUIRE(std::isfinite(*seasonal_summary.bic));
		}
	}
}

TEST_CASE("Quick auto-select evaluates multiple candidates", "[integration][quick][auto_select]") {
	std::vector<double> data;
	for (int i = 0; i < 40; ++i) {
		data.push_back(10.0 + 0.5 * static_cast<double>(i));
	}

	anofoxtime::quick::AutoSelectOptions options;
	options.horizon = 3;
	options.include_backtest = true;
	options.backtest_config.min_train = 10;
	options.backtest_config.max_folds = 2;
	options.backtest_config.step = 1;

	const auto result = anofoxtime::quick::autoSelect(data, options);
	REQUIRE_FALSE(result.model_name.empty());
	REQUIRE_FALSE(result.candidates.empty());
	REQUIRE(result.forecast.forecast.horizon() == static_cast<std::size_t>(options.horizon));
	REQUIRE(result.candidates.front().forecast.forecast.horizon() == static_cast<std::size_t>(options.horizon));
}

TEST_CASE("Quick auto-select honours preprocessing pipeline", "[integration][quick][auto_select][transform]") {
	std::vector<double> data{0.25, 0.32, 0.41, 0.36, 0.44, 0.47, 0.52, 0.49, 0.55, 0.58};

	anofoxtime::quick::AutoSelectOptions options;
	options.horizon = 2;
	options.include_backtest = false;
	options.sma_windows = {3};
	options.ses_alphas = {0.4};
	options.holt_params.clear();
	options.arima_orders = {{}};
	options.pipeline_factory = [] {
		std::vector<std::unique_ptr<anofoxtime::transform::Transformer>> transformers;
		transformers.push_back(std::make_unique<anofoxtime::transform::Logit>());
		return std::make_unique<anofoxtime::transform::Pipeline>(std::move(transformers));
	};

	const auto result = anofoxtime::quick::autoSelect(data, options);
	REQUIRE(result.forecast.forecast.horizon() == 2);
	const auto &forecast = result.forecast.forecast.series();
	for (double value : forecast) {
		REQUIRE(value > 0.0);
		REQUIRE(value < 1.0);
	}
}

TEST_CASE("Rolling backtest applies preprocessing pipeline", "[integration][quick][backtest][transform]") {
	std::vector<double> data{0.2, 0.28, 0.31, 0.35, 0.33, 0.4, 0.43, 0.45, 0.47, 0.5};
	anofoxtime::validation::RollingCVConfig config;
	config.min_train = 5;
	config.horizon = 2;
	config.max_folds = 2;
	config.step = 1;

	auto pipeline_factory = [] {
		std::vector<std::unique_ptr<anofoxtime::transform::Transformer>> transformers;
		transformers.push_back(std::make_unique<anofoxtime::transform::Logit>());
		return std::make_unique<anofoxtime::transform::Pipeline>(std::move(transformers));
	};

	const auto summary =
	    anofoxtime::quick::rollingBacktestSMA(data, config, 3, {}, pipeline_factory);
	REQUIRE_FALSE(summary.folds.empty());
	for (const auto &fold : summary.folds) {
		const auto &series = fold.forecast.series();
		for (double value : series) {
			REQUIRE(value > 0.0);
			REQUIRE(value < 1.0);
		}
	}
}
