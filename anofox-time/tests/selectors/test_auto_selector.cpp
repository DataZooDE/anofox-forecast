#include <catch2/catch_test_macros.hpp>

#include "anofox-time/selectors/auto_selector.hpp"

#include <vector>

using anofoxtime::selectors::AutoSelector;
using anofoxtime::selectors::CandidateModel;

TEST_CASE("AutoSelector picks lowest-scoring candidate", "[selectors][auto_selector]") {
	CandidateModel sma_short{};
	sma_short.type = CandidateModel::Type::SimpleMovingAverage;
	sma_short.window = 2;

	CandidateModel sma_long{};
	sma_long.type = CandidateModel::Type::SimpleMovingAverage;
	sma_long.window = 5;

	AutoSelector selector({sma_short, sma_long});
	selector.withScoringFunction([](const anofoxtime::utils::AccuracyMetrics& metrics) {
		return metrics.mae;
	});

	std::vector<double> train{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	std::vector<double> actual{11, 12, 13};
	const auto result = selector.select(train, actual);

	REQUIRE(result.best.model.type == CandidateModel::Type::SimpleMovingAverage);
	REQUIRE(result.best.model.window == 2);
	REQUIRE_FALSE(result.ranked.empty());
	REQUIRE(result.ranked.front().score <= result.ranked.back().score);
}

TEST_CASE("AutoSelector validates inputs", "[selectors][auto_selector][error]") {
	AutoSelector selector;
	std::vector<double> empty;
	std::vector<double> actual{1.0};
	REQUIRE_THROWS_AS(selector.select(empty, actual), std::invalid_argument);
	REQUIRE_THROWS_AS(selector.select({1.0}, {}), std::invalid_argument);
	REQUIRE_THROWS_AS(selector.select({1.0, 2.0}, actual, std::vector<double>{1.0, 2.0}), std::invalid_argument);
}

TEST_CASE("AutoSelector cross-validation aggregates scores", "[selectors][auto_selector][cv]") {
	CandidateModel sma_short{};
	sma_short.type = CandidateModel::Type::SimpleMovingAverage;
	sma_short.window = 2;

	CandidateModel ses{};
	ses.type = CandidateModel::Type::SimpleExponentialSmoothing;
	ses.alpha = 0.5;

	AutoSelector selector({sma_short, ses});
	selector.withScoringFunction([](const anofoxtime::utils::AccuracyMetrics& metrics) {
		return metrics.rmse;
	});

	std::vector<double> data;
	for (int i = 1; i <= 40; ++i) {
		data.push_back(static_cast<double>(i));
	}

	const auto result = selector.selectWithCV(data, 3, 10, 2);
	REQUIRE_FALSE(result.ranked.empty());
	REQUIRE(result.best.score <= result.ranked.back().score);
}
