#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/utils/metrics.hpp"
#include "anofox-time/validation.hpp"
#include "anofox-time/models/sma.hpp"
#include "common/time_series_helpers.hpp"

#include <chrono>
#include <cmath>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <vector>

#include "common/metrics_helpers.hpp"

using anofoxtime::utils::AccuracyMetrics;

namespace {

AccuracyMetrics computeExpectedAccuracy(const std::vector<double> &actual,
                                        const std::vector<double> &predicted,
                                        const std::optional<std::vector<double>> &baseline = std::nullopt) {
	return anofoxtime::validation::accuracyMetrics(actual, predicted, baseline);
}

} // namespace

TEST_CASE("Accuracy metrics aggregate scalar series", "[validation][accuracy]") {
	const std::vector<double> actual{1.0, 2.0, 3.0};
	const std::vector<double> predicted{1.1, 1.9, 2.8};
	const std::vector<double> baseline{0.9, 2.1, 3.1};

	const auto metrics = anofoxtime::validation::accuracyMetrics(actual, predicted, baseline);

	REQUIRE(metrics.n == actual.size());
	REQUIRE(metrics.mae == Catch::Approx(0.1333333333));
	REQUIRE(metrics.rmse == Catch::Approx(std::sqrt(0.02))); // (0.01 + 0.01 + 0.04)/3
	REQUIRE(metrics.mape.has_value());
	REQUIRE(metrics.smape.has_value());
	REQUIRE(metrics.mase.has_value());
	REQUIRE(metrics.r_squared.has_value());
	REQUIRE_FALSE(metrics.isMultivariate());
}

TEST_CASE("Accuracy metrics validate input dimensions", "[validation][accuracy][error]") {
	const std::vector<double> actual{1.0, 2.0};
	const std::vector<double> predicted{1.0};
	REQUIRE_THROWS_AS(anofoxtime::validation::accuracyMetrics(actual, predicted), std::invalid_argument);

	const std::vector<std::vector<double>> multi_actual{{1.0, 2.0}, {3.0, 4.0}};
	const std::vector<std::vector<double>> multi_predicted{{1.0, 2.0}};
	REQUIRE_THROWS_AS(anofoxtime::validation::accuracyMetrics(multi_actual, multi_predicted),
	                  std::invalid_argument);

	const std::vector<std::vector<double>> baseline_mismatch{{1.0, 2.0}};
	REQUIRE_THROWS_AS(anofoxtime::validation::accuracyMetrics(multi_actual, multi_actual, baseline_mismatch),
	                  std::invalid_argument);
}

TEST_CASE("Accuracy metrics aggregate multivariate series", "[validation][accuracy][multivariate]") {
	const std::vector<std::vector<double>> actual{{1.0, 2.0, 3.0}, {10.0, 11.0, 12.0}};
	const std::vector<std::vector<double>> predicted{{0.9, 2.1, 2.9}, {9.5, 11.5, 12.5}};
	const std::vector<std::vector<double>> baseline{{0.8, 1.9, 3.2}, {9.0, 10.5, 11.5}};

	const auto metrics = anofoxtime::validation::accuracyMetrics(actual, predicted, baseline);

	REQUIRE(metrics.n == actual.front().size());
	REQUIRE(metrics.isMultivariate());
	REQUIRE(metrics.per_dimension.size() == actual.size());

	const auto expected_dim0 =
	    computeExpectedAccuracy(actual[0], predicted[0], std::optional<std::vector<double>>(baseline[0]));
	const auto expected_dim1 =
	    computeExpectedAccuracy(actual[1], predicted[1], std::optional<std::vector<double>>(baseline[1]));

	tests::helpers::expectAccuracyApprox(metrics.per_dimension[0], expected_dim0);
	tests::helpers::expectAccuracyApprox(metrics.per_dimension[1], expected_dim1);
	REQUIRE(metrics.mase.has_value());
}

TEST_CASE("Time split partitions series respecting ratio", "[validation][split]") {
	auto data = tests::helpers::linearSeries(1.0, 1.0, 5);
	const auto split = anofoxtime::validation::timeSplit(data, 0.6);
	REQUIRE(split.train.size() == 3);
	REQUIRE(split.test.size() == 2);
	REQUIRE(split.train.front() == Catch::Approx(1.0));
	REQUIRE(split.test.back() == Catch::Approx(5.0));

	REQUIRE_THROWS_AS(anofoxtime::validation::timeSplit({}, 0.5), std::invalid_argument);
	REQUIRE_THROWS_AS(anofoxtime::validation::timeSplit(data, 0.0), std::invalid_argument);
	REQUIRE_THROWS_AS(anofoxtime::validation::timeSplit(data, 0.99), std::invalid_argument);
}

TEST_CASE("TimeSeries split preserves attributes", "[validation][split][time_series]") {
	auto series = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0, 4.0, 5.0});
	series.setMetadata({{"id", "TS-1"}});
	series.setDimensionMetadata(0, anofoxtime::core::TimeSeries::Metadata{{"unit", "kWh"}});
	series.setTimezone(anofoxtime::core::TimeSeries::TimeZoneInfo{"UTC", std::chrono::minutes{0}});
	series.setFrequency(std::chrono::seconds{300});

	const auto split = anofoxtime::validation::timeSplit(series, 0.4);
	REQUIRE(split.train.size() == 2);
	REQUIRE(split.test.size() == 3);
	REQUIRE(split.train.metadata().at("id") == "TS-1");
	REQUIRE(split.test.metadata().at("id") == "TS-1");
	REQUIRE(split.train.timezone()->name == "UTC");
	REQUIRE(split.test.frequency() == std::optional<std::chrono::nanoseconds>(std::chrono::seconds{300}));
}

TEST_CASE("Time series cross-validation yields rolling windows", "[validation][cv]") {
	auto data = tests::helpers::linearSeries(1.0, 1.0, 10);

	const auto splits = anofoxtime::validation::timeSeriesCV(data, 3, 4, 2);
	REQUIRE(splits.size() == 3);

	for (std::size_t i = 0; i < splits.size(); ++i) {
		const auto &split = splits[i];
		REQUIRE(split.train.size() >= 4);
		REQUIRE(split.test.size() == 2);
		REQUIRE(split.train.back() < split.test.front());
	}

	REQUIRE_THROWS_AS(anofoxtime::validation::timeSeriesCV({}, 1, 1, 1), std::invalid_argument);
	REQUIRE_THROWS_AS(anofoxtime::validation::timeSeriesCV(data, 0, 1, 1), std::invalid_argument);
	REQUIRE_THROWS_AS(anofoxtime::validation::timeSeriesCV(data, 2, 0, 1), std::invalid_argument);
	REQUIRE_THROWS_AS(anofoxtime::validation::timeSeriesCV(data, 2, 1, 0), std::invalid_argument);
	REQUIRE_THROWS_AS(anofoxtime::validation::timeSeriesCV(std::vector<double>{1, 2, 3}, 2, 3, 2),
	                  std::invalid_argument);
}

TEST_CASE("Rolling window CV yields expanding splits", "[validation][cv][time_series]") {
	auto series = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
	series.setMetadata({{"id", "TS-2"}});
	anofoxtime::validation::RollingCVConfig config;
	config.min_train = 3;
	config.horizon = 2;
	config.step = 1;
	config.max_folds = 2;
	config.expanding = true;

	const auto splits = anofoxtime::validation::rollingWindowCV(series, config);
	REQUIRE(splits.size() == 2);
	REQUIRE(splits[0].train.size() == 3);
	REQUIRE(splits[0].test.size() == 2);
	REQUIRE(splits[1].train.size() == 4);
	REQUIRE(splits[1].train.metadata().at("id") == "TS-2");
	REQUIRE(splits[0].train.getValues().front() == Catch::Approx(1.0));
	REQUIRE(splits[1].train.getValues().front() == Catch::Approx(1.0));
}

TEST_CASE("Rolling window CV supports sliding windows", "[validation][cv][time_series][sliding]") {
	auto series = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0});
	anofoxtime::validation::RollingCVConfig config;
	config.min_train = 3;
	config.horizon = 2;
	config.step = 2;
	config.max_folds = 2;
	config.expanding = false;

	const auto splits = anofoxtime::validation::rollingWindowCV(series, config);
	REQUIRE(splits.size() == 2);
	REQUIRE(splits[0].train.getValues().front() == Catch::Approx(1.0));
	REQUIRE(splits[1].train.getValues().front() == Catch::Approx(3.0));
	REQUIRE(splits[1].train.size() == 3);
}

TEST_CASE("Rolling window CV validates configuration", "[validation][cv][time_series][errors]") {
	auto series = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0, 4.0});
	anofoxtime::validation::RollingCVConfig config;
	config.min_train = 0;
	REQUIRE_THROWS_AS(anofoxtime::validation::rollingWindowCV(series, config), std::invalid_argument);

	config.min_train = 3;
	config.horizon = 0;
	REQUIRE_THROWS_AS(anofoxtime::validation::rollingWindowCV(series, config), std::invalid_argument);

	config.horizon = 1;
	config.step = 0;
	REQUIRE_THROWS_AS(anofoxtime::validation::rollingWindowCV(series, config), std::invalid_argument);

	config.step = 1;
	config.max_folds = 0;
	REQUIRE_THROWS_AS(anofoxtime::validation::rollingWindowCV(series, config), std::invalid_argument);

	config.max_folds = 2;
	config.expanding = true;
	config.min_train = 3;
	config.horizon = 2;
	const auto short_series = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0});
	REQUIRE_THROWS_AS(anofoxtime::validation::rollingWindowCV(short_series, config), std::invalid_argument);
}

TEST_CASE("Rolling backtest aggregates fold metrics", "[validation][backtest]") {
	auto series = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
	anofoxtime::validation::RollingCVConfig config;
	config.min_train = 3;
	config.horizon = 2;
	config.step = 1;
	config.max_folds = 2;
	config.expanding = true;

	const auto summary = anofoxtime::validation::rollingBacktest(
	    series, config,
	    []() { return anofoxtime::models::SimpleMovingAverageBuilder().withWindow(2).build(); });

	REQUIRE(summary.folds.size() == 2);

	const auto splits = anofoxtime::validation::rollingWindowCV(series, config);
	std::vector<double> joined_actual;
	std::vector<double> joined_predicted;
	for (std::size_t i = 0; i < summary.folds.size(); ++i) {
		const auto &fold = summary.folds[i];
		REQUIRE(fold.index == i);
		REQUIRE(fold.train_size == splits[i].train.size());
		REQUIRE(fold.test_size == splits[i].test.size());
		REQUIRE(fold.forecast.primary().size() == splits[i].test.size());
		joined_actual.insert(joined_actual.end(), splits[i].test.getValues().begin(), splits[i].test.getValues().end());
		joined_predicted.insert(joined_predicted.end(), fold.forecast.primary().begin(), fold.forecast.primary().end());
	}

	const auto expected = anofoxtime::validation::accuracyMetrics(joined_actual, joined_predicted);
	tests::helpers::expectAccuracyApprox(summary.aggregate, expected, 1e-6);
}

TEST_CASE("Rolling backtest supports baseline provider", "[validation][backtest][baseline]") {
	auto series = tests::helpers::makeUnivariateSeries({1.0, 1.5, 2.0, 2.5, 3.0, 3.5});
	anofoxtime::validation::RollingCVConfig config;
	config.min_train = 3;
	config.horizon = 2;
	config.step = 1;
	config.max_folds = 2;

	auto summary = anofoxtime::validation::rollingBacktest(
	    series, config,
	    []() { return anofoxtime::models::SimpleMovingAverageBuilder().withWindow(2).build(); },
	    [](const anofoxtime::core::TimeSeries &train, const anofoxtime::core::TimeSeries &test) {
		const double last_train = train.getValues().back();
		return std::vector<double>(test.size(), last_train);
	});

	REQUIRE(summary.folds.size() == 2);
	for (const auto &fold : summary.folds) {
		REQUIRE(fold.metrics.mase.has_value());
	}
	REQUIRE(summary.aggregate.mase.has_value());
}

TEST_CASE("Rolling backtest validates factory output", "[validation][backtest][errors]") {
	auto series = tests::helpers::makeUnivariateSeries({1.0, 2.0, 3.0, 4.0});
	anofoxtime::validation::RollingCVConfig config;
	config.min_train = 3;
	config.horizon = 1;

	REQUIRE_THROWS_AS(
	    anofoxtime::validation::rollingBacktest(series, config, []() { return std::unique_ptr<anofoxtime::models::IForecaster>{}; }),
	    std::runtime_error);
}
