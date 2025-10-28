#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/models/dtw.hpp"

#include <cmath>
#include <stdexcept>
#include <vector>

using anofoxtime::models::DTWBuilder;
using anofoxtime::models::DtwMetric;

TEST_CASE("DTW builder validates bounds", "[models][dtw][builder]") {
	DTWBuilder builder;
	REQUIRE_THROWS_AS(builder.withMaxDistance(-1.0), std::invalid_argument);
	REQUIRE_THROWS_AS(builder.withLowerBound(-0.5), std::invalid_argument);
	REQUIRE_THROWS_AS(builder.withUpperBound(-0.5), std::invalid_argument);
}

TEST_CASE("DTW distance zero for identical series", "[models][dtw]") {
	auto dtw = DTWBuilder().withMetric(DtwMetric::Euclidean).build();
	const std::vector<double> series{1.0, 2.0, 3.0};
	REQUIRE(dtw->distance(series, series) == Catch::Approx(0.0));
}

TEST_CASE("DTW Euclidean distance matches known result", "[models][dtw][euclidean]") {
	auto dtw = DTWBuilder().withMetric(DtwMetric::Euclidean).build();
	const std::vector<double> lhs{1.0, 2.0, 3.0};
	const std::vector<double> rhs{2.0, 2.0, 4.0};

	const double distance = dtw->distance(lhs, rhs);
	// Expected warping aligns (1,2,3) with (2,2,4) giving sqrt((1-2)^2 + (2-2)^2 + (3-4)^2) = sqrt(2)
	REQUIRE(distance == Catch::Approx(std::sqrt(2.0)).margin(1e-6));
}

TEST_CASE("DTW Manhattan metric uses absolute differences", "[models][dtw][manhattan]") {
	auto dtw = DTWBuilder().withMetric(DtwMetric::Manhattan).build();
	const std::vector<double> lhs{1.0, 3.0};
	const std::vector<double> rhs{2.0, 4.0};

	const double distance = dtw->distance(lhs, rhs);
	REQUIRE(distance == Catch::Approx(2.0));
}

TEST_CASE("DTW distance matrix is symmetric", "[models][dtw][matrix]") {
	auto dtw = DTWBuilder().withMetric(DtwMetric::Euclidean).withWindow(1).build();
	const std::vector<std::vector<double>> series{
	    {0.0, 1.0},
	    {0.0, 2.0},
	    {1.0, 3.0},
	};

	const auto matrix = dtw->distanceMatrix(series);
	REQUIRE(matrix.size() == 3);
	REQUIRE(matrix.at(0, 1) == Catch::Approx(matrix.at(1, 0)));
	REQUIRE(matrix.at(1, 2) == Catch::Approx(matrix.at(2, 1)));
	REQUIRE(matrix.at(0, 0) == Catch::Approx(0.0));
}
