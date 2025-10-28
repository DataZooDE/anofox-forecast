#include <catch2/catch_test_macros.hpp>

#include "anofox-time/clustering/dbscan.hpp"

#include <vector>

using anofoxtime::clustering::DbscanBuilder;
using anofoxtime::clustering::DbscanClusterer;
using anofoxtime::core::DistanceMatrix;

namespace {

DistanceMatrix makeSimpleMatrix() {
	DistanceMatrix::Matrix data{{0.0, 0.4, 0.5, 2.0},
	                           {0.4, 0.0, 0.6, 2.1},
	                           {0.5, 0.6, 0.0, 2.2},
	                           {2.0, 2.1, 2.2, 0.0}};
	return DistanceMatrix::fromSquare(std::move(data));
}

} // namespace

TEST_CASE("DBSCAN builder validates parameters", "[clustering][dbscan][builder]") {
	DbscanBuilder builder;
	REQUIRE_THROWS_AS(builder.withEpsilon(-1.0), std::invalid_argument);
	REQUIRE_THROWS_AS(builder.withMinClusterSize(0), std::invalid_argument);

	REQUIRE_NOTHROW(builder.withEpsilon(0.5).withMinClusterSize(2).build());
}

TEST_CASE("DBSCAN clusters dense neighbourhoods", "[clustering][dbscan]") {
	auto matrix = makeSimpleMatrix();
	auto clusterer = DbscanBuilder().withEpsilon(0.7).withMinClusterSize(2).build();
	const auto clusters = clusterer->cluster(matrix);

	REQUIRE(clusters.size() == 4);
	REQUIRE(clusters[0].isCluster());
	REQUIRE(clusters[1].isCluster());
	REQUIRE(clusters[2].isCluster());
	REQUIRE(clusters[3].isNoise());

	const auto labels = clusterer->clusterLabels(matrix);
	REQUIRE(labels[0] == labels[1]);
	REQUIRE(labels[1] == labels[2]);
	REQUIRE(labels[3] == -1);
}
