#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/outlier/dbscan_outlier.hpp"

#include <vector>

using anofoxtime::core::DistanceMatrix;
using anofoxtime::outlier::DbscanOutlierBuilder;

namespace {

DistanceMatrix makeOutlierMatrix() {
	DistanceMatrix::Matrix data{{0.0, 0.3, 0.4, 1.8},
	                           {0.3, 0.0, 0.5, 1.7},
	                           {0.4, 0.5, 0.0, 1.9},
	                           {1.8, 1.7, 1.9, 0.0}};
	return DistanceMatrix::fromSquare(std::move(data));
}

} // namespace

TEST_CASE("DBSCAN outlier builder mirrors clustering validation", "[outlier][dbscan][builder]") {
	DbscanOutlierBuilder builder;
	REQUIRE_THROWS_AS(builder.withEpsilon(-0.1), std::invalid_argument);
	REQUIRE_THROWS_AS(builder.withMinClusterSize(0), std::invalid_argument);

	REQUIRE_NOTHROW(builder.withEpsilon(0.6).withMinClusterSize(2).build());
}

TEST_CASE("DBSCAN outlier flags minority cluster", "[outlier][dbscan]") {
	auto detector = DbscanOutlierBuilder().withEpsilon(0.6).withMinClusterSize(2).build();
	const auto result = detector->detect(makeOutlierMatrix());

	REQUIRE(result.series_results.size() == 4);
	REQUIRE(result.outlying_series.size() == 1);
	REQUIRE(result.outlying_series.front() == 3);
	REQUIRE_FALSE(result.series_results[0].is_outlier);
	REQUIRE(result.series_results[3].is_outlier);
	REQUIRE(result.series_results[3].scores.front() == Catch::Approx(1.0));
}
