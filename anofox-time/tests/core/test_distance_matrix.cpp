#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "anofox-time/core/distance_matrix.hpp"

#include <algorithm>
#include <vector>

using anofoxtime::core::DistanceMatrix;

TEST_CASE("DistanceMatrix validates square input", "[core][distance_matrix][validation]") {
	DistanceMatrix::Matrix valid{{0.0, 1.0, 2.0}, {1.0, 0.0, 1.5}, {2.0, 1.5, 0.0}};
	REQUIRE_NOTHROW(DistanceMatrix(valid));

	DistanceMatrix::Matrix invalid{{0.0, 1.0}, {1.0}};
	REQUIRE_THROWS_AS(DistanceMatrix(invalid), std::invalid_argument);
}

TEST_CASE("DistanceMatrix offers convenient accessors", "[core][distance_matrix]") {
	DistanceMatrix matrix({{0.0, 1.0}, {1.0, 0.0}});

	REQUIRE(matrix.size() == 2);
	REQUIRE_FALSE(matrix.empty());

	const auto [rows, cols] = matrix.shape();
	REQUIRE(rows == 2);
	REQUIRE(cols == 2);

	REQUIRE(matrix.at(0, 1) == Catch::Approx(1.0));

	matrix[1][0] = 2.0;
	REQUIRE(matrix.at(1, 0) == Catch::Approx(2.0));

	const auto &data = matrix.data();
	REQUIRE(data.size() == 2);
	REQUIRE(data[0][0] == Catch::Approx(0.0));
}

TEST_CASE("DistanceMatrix supports iteration", "[core][distance_matrix][iteration]") {
	DistanceMatrix matrix({{0.0, 1.0}, {1.0, 0.0}});

	const auto count = std::distance(matrix.begin(), matrix.end());
	REQUIRE(count == static_cast<std::ptrdiff_t>(matrix.size()));
}

TEST_CASE("DistanceMatrix factory mirrors constructor validation", "[core][distance_matrix][factory]") {
	auto matrix = DistanceMatrix::fromSquare({{0.0, 1.0, 2.0}, {1.0, 0.0, 1.5}, {2.0, 1.5, 0.0}});
	REQUIRE(matrix.size() == 3);
	REQUIRE(matrix.at(0, 2) == Catch::Approx(2.0));
}
