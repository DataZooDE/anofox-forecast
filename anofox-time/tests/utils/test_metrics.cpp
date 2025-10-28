#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "anofox-time/utils/metrics.hpp"

using anofoxtime::utils::Metrics;

TEST_CASE("Metrics compute basic error statistics", "[utils][metrics]") {
	const std::vector<double> actual{1.0, 2.0, 3.0};
	const std::vector<double> predicted{1.5, 2.5, 2.0};

    const double mae = Metrics::mae(actual, predicted);
    const double mse = Metrics::mse(actual, predicted);
    const double rmse = Metrics::rmse(actual, predicted);

    const double expected_mae = (0.5 + 0.5 + 1.0) / 3.0;
    const double expected_mse = (0.25 + 0.25 + 1.0) / 3.0;
    const double expected_rmse = std::sqrt(expected_mse);

    REQUIRE(mae == Catch::Approx(expected_mae));
    REQUIRE(mse == Catch::Approx(expected_mse));
    REQUIRE(rmse == Catch::Approx(expected_rmse));

    const auto mape = Metrics::mape(actual, predicted);
    REQUIRE(mape.has_value());
    const double expected_mape =
        ((0.5 / 1.0) + (0.5 / 2.0) + (1.0 / 3.0)) / 3.0 * 100.0;
    REQUIRE(*mape == Catch::Approx(expected_mape).margin(1e-6));

    const auto smape = Metrics::smape(actual, predicted);
    REQUIRE(smape.has_value());
    const double expected_smape =
        ((0.5 / ((1.0 + 1.5) / 2.0)) + (0.5 / ((2.0 + 2.5) / 2.0)) +
         (1.0 / ((3.0 + 2.0) / 2.0))) /
        3.0 * 100.0;
    REQUIRE(*smape == Catch::Approx(expected_smape).margin(1e-6));
}

TEST_CASE("Metrics handles invalid inputs", "[utils][metrics][error]") {
	const std::vector<double> actual{1.0, 2.0};
	const std::vector<double> predicted{1.0};

	REQUIRE_THROWS_AS(Metrics::mae(actual, predicted), std::invalid_argument);
	REQUIRE_THROWS_AS(Metrics::mape(actual, predicted), std::invalid_argument);
	REQUIRE_THROWS_AS(Metrics::mase(actual, predicted, actual), std::invalid_argument);
}

TEST_CASE("Metrics MASE and optional outputs", "[utils][metrics][mase]") {
    const std::vector<double> actual{2.0, 4.0, 6.0, 8.0};
    const std::vector<double> predicted{2.0, 5.0, 7.0, 8.0};
    const std::vector<double> naive{1.0, 3.0, 5.0, 7.0};

	const auto mase = Metrics::mase(actual, predicted, naive);
	REQUIRE(mase.has_value());
    REQUIRE(*mase == Catch::Approx(0.5));

	SECTION("Baseline with zero error returns nullopt") {
		const std::vector<double> identical = actual;
		const auto mase_zero_baseline = Metrics::mase(actual, identical, actual);
		REQUIRE_FALSE(mase_zero_baseline.has_value());
	}
}

TEST_CASE("Metrics MAPE and SMAPE skip zero denominators", "[utils][metrics][optional]") {
	const std::vector<double> actual{0.0, 0.0};
	const std::vector<double> predicted{1.0, 2.0};

	REQUIRE_FALSE(Metrics::mape(actual, predicted).has_value());

	const std::vector<double> smape_actual{0.0, 1.0};
	const std::vector<double> smape_predicted{0.0, 2.0};
	const auto smape = Metrics::smape(smape_actual, smape_predicted);
	REQUIRE(smape.has_value());
	REQUIRE(*smape == Catch::Approx(66.666666).margin(1e-5));
}

TEST_CASE("Metrics R2 handles degenerate variance", "[utils][metrics][r2]") {
	const std::vector<double> actual{5.0, 5.0, 5.0};
	const std::vector<double> predicted{4.0, 5.0, 6.0};

	REQUIRE_FALSE(Metrics::r2(actual, predicted).has_value());

	const std::vector<double> varying_actual{1.0, 2.0, 3.0};
	const std::vector<double> varying_predicted{1.1, 1.9, 3.1};
	const auto r2 = Metrics::r2(varying_actual, varying_predicted);
	REQUIRE(r2.has_value());
	REQUIRE(*r2 == Catch::Approx(0.985).margin(1e-6));
}
