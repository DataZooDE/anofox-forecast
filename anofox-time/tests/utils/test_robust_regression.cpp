#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "anofox-time/utils/robust_regression.hpp"
#include <vector>
#include <algorithm>

using namespace anofoxtime::utils::RobustRegression;

TEST_CASE("RobustRegression median with odd number of elements", "[utils][robust_regression]") {
	std::vector<double> data{3.0, 1.0, 4.0, 2.0, 5.0};
	double result = median(data);
	REQUIRE(result == 3.0);
}

TEST_CASE("RobustRegression median with even number of elements", "[utils][robust_regression]") {
	std::vector<double> data{3.0, 1.0, 4.0, 2.0};
	double result = median(data);
	REQUIRE(result == Catch::Approx(2.5));
}

TEST_CASE("RobustRegression median with single element", "[utils][robust_regression]") {
	std::vector<double> data{5.0};
	double result = median(data);
	REQUIRE(result == 5.0);
}

TEST_CASE("RobustRegression median with empty vector", "[utils][robust_regression][error]") {
	std::vector<double> data;
	REQUIRE_THROWS_AS(median(data), std::invalid_argument);
}

TEST_CASE("RobustRegression median with negative values", "[utils][robust_regression]") {
	std::vector<double> data{-3.0, -1.0, -4.0, -2.0};
	double result = median(data);
	REQUIRE(result == Catch::Approx(-2.5));
}

TEST_CASE("RobustRegression median with duplicates", "[utils][robust_regression]") {
	std::vector<double> data{3.0, 3.0, 3.0, 1.0, 5.0};
	double result = median(data);
	REQUIRE(result == 3.0);
}

TEST_CASE("RobustRegression siegelRepeatedMedians basic", "[utils][robust_regression]") {
	std::vector<double> x{1.0, 2.0, 3.0, 4.0, 5.0};
	std::vector<double> y{2.0, 4.0, 6.0, 8.0, 10.0};  // y = 2*x
	
	double slope, intercept;
	siegelRepeatedMedians(x, y, slope, intercept);
	
	REQUIRE(slope == Catch::Approx(2.0).margin(0.1));
	REQUIRE(intercept == Catch::Approx(0.0).margin(0.1));
}

TEST_CASE("RobustRegression siegelRepeatedMedians with intercept", "[utils][robust_regression]") {
	std::vector<double> x{1.0, 2.0, 3.0, 4.0, 5.0};
	std::vector<double> y{3.0, 5.0, 7.0, 9.0, 11.0};  // y = 2*x + 1
	
	double slope, intercept;
	siegelRepeatedMedians(x, y, slope, intercept);
	
	REQUIRE(slope == Catch::Approx(2.0).margin(0.2));
	REQUIRE(intercept == Catch::Approx(1.0).margin(0.2));
}

TEST_CASE("RobustRegression siegelRepeatedMedians with mismatched sizes", "[utils][robust_regression][error]") {
	std::vector<double> x{1.0, 2.0, 3.0};
	std::vector<double> y{2.0, 4.0};
	
	double slope, intercept;
	REQUIRE_THROWS_AS(siegelRepeatedMedians(x, y, slope, intercept), std::invalid_argument);
}

TEST_CASE("RobustRegression siegelRepeatedMedians with empty vectors", "[utils][robust_regression][error]") {
	std::vector<double> x;
	std::vector<double> y;
	
	double slope, intercept;
	REQUIRE_THROWS_AS(siegelRepeatedMedians(x, y, slope, intercept), std::invalid_argument);
}

TEST_CASE("RobustRegression siegelRepeatedMedians with single point", "[utils][robust_regression]") {
	std::vector<double> x{1.0};
	std::vector<double> y{2.0};
	
	double slope, intercept;
	// May throw or handle gracefully depending on implementation
	// Just verify it doesn't crash
	REQUIRE_NOTHROW(siegelRepeatedMedians(x, y, slope, intercept));
}

