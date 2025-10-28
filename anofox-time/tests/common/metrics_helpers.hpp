#pragma once

#include "anofox-time/utils/metrics.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <optional>
#include <vector>

namespace tests::helpers {

inline std::vector<double> linearSeries(double start, double step, std::size_t count) {
	std::vector<double> values;
	values.reserve(count);
	for (std::size_t i = 0; i < count; ++i) {
		values.push_back(start + static_cast<double>(i) * step);
	}
	return values;
}

inline void expectAccuracyApprox(const anofoxtime::utils::AccuracyMetrics &actual,
                                 const anofoxtime::utils::AccuracyMetrics &expected,
                                 double tol = 1e-6) {
	REQUIRE(actual.n == expected.n);
	REQUIRE(actual.per_dimension.size() == expected.per_dimension.size());

	REQUIRE(actual.mae == Catch::Approx(expected.mae).margin(tol));
	REQUIRE(actual.mse == Catch::Approx(expected.mse).margin(tol));
	REQUIRE(actual.rmse == Catch::Approx(expected.rmse).margin(tol));

	const auto compareOptional = [tol](const std::optional<double> &lhs, const std::optional<double> &rhs) {
		REQUIRE(lhs.has_value() == rhs.has_value());
		if (lhs && rhs) {
			REQUIRE(*lhs == Catch::Approx(*rhs).margin(tol));
		}
	};

	compareOptional(actual.mape, expected.mape);
	compareOptional(actual.smape, expected.smape);
	compareOptional(actual.mase, expected.mase);
	compareOptional(actual.r_squared, expected.r_squared);

	for (std::size_t i = 0; i < actual.per_dimension.size(); ++i) {
		expectAccuracyApprox(actual.per_dimension[i], expected.per_dimension[i], tol);
	}
}

} // namespace tests::helpers
