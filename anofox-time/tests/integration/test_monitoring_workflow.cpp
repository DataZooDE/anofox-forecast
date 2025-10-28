#include <catch2/catch_test_macros.hpp>

#include "anofox-time/quick.hpp"
#include "anofox-time/validation.hpp"
#include "common/metrics_helpers.hpp"
#include "common/monitoring_fixtures.hpp"

#include <algorithm>
#include <cmath>

using tests::fixtures::monitoringSignal;
using tests::fixtures::monitoringPointAnomalies;
using tests::fixtures::monitoringChangepoints;
using tests::fixtures::monitoringSegmentOutliers;
using tests::fixtures::monitoringWindows;
using tests::fixtures::monitoringCVConfig;

TEST_CASE("Monitoring workflow diagnostics remain stable", "[integration][monitoring]") {
	const auto signal = monitoringSignal();
	REQUIRE(signal.size() == 384);

	const auto mad_outliers = anofoxtime::quick::detectOutliersMAD(signal, 3.0);
	REQUIRE(mad_outliers.outlier_indices == monitoringPointAnomalies());

	const auto changepoints = anofoxtime::quick::detectChangepoints(signal, 180.0);
	REQUIRE(changepoints == monitoringChangepoints());

	const auto windows = monitoringWindows(signal);
	REQUIRE_FALSE(windows.empty());

	const auto segment_outliers = anofoxtime::quick::detectOutliersDBSCAN(windows, 12.0, 2);
	REQUIRE(segment_outliers.outlying_series == monitoringSegmentOutliers());

	auto cfg = monitoringCVConfig();
	const auto backtest = anofoxtime::quick::rollingBacktestARIMA(signal, cfg, 1, 1, 1, true);
	REQUIRE(backtest.aggregate.n == cfg.horizon * backtest.folds.size());

	REQUIRE(backtest.aggregate.mae == Catch::Approx(4.1857).margin(1e-3));
	REQUIRE(backtest.aggregate.rmse == Catch::Approx(5.5431).margin(1e-3));
	REQUIRE(backtest.aggregate.smape.has_value());
	REQUIRE(*backtest.aggregate.smape == Catch::Approx(5.5551).margin(1e-3));
	REQUIRE(backtest.aggregate.r_squared.has_value());
	REQUIRE(*backtest.aggregate.r_squared == Catch::Approx(0.4781).margin(1e-3));

	const auto worst = std::max_element(
	    backtest.folds.begin(),
	    backtest.folds.end(),
	    [](const auto &lhs, const auto &rhs) { return lhs.metrics.mae < rhs.metrics.mae; });
	REQUIRE(worst != backtest.folds.end());
	REQUIRE(worst->index == 0);
	REQUIRE(worst->train_size == 96);
	REQUIRE(worst->test_size == 24);
	REQUIRE(worst->metrics.mae == Catch::Approx(5.1052).margin(1e-3));
	REQUIRE(worst->metrics.rmse == Catch::Approx(7.1035).margin(1e-3));
	REQUIRE(worst->metrics.smape.has_value());
	REQUIRE(*worst->metrics.smape == Catch::Approx(7.1159).margin(1e-3));
	if (worst->metrics.mase.has_value()) {
		REQUIRE(std::isfinite(*worst->metrics.mase));
	}
	REQUIRE(worst->metrics.r_squared.has_value());
	REQUIRE(*worst->metrics.r_squared == Catch::Approx(0.0077).margin(1e-3));
}
