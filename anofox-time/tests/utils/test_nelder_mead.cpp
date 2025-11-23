#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "anofox-time/utils/nelder_mead.hpp"
#include <vector>
#include <cmath>

using namespace anofoxtime::utils;

TEST_CASE("NelderMeadOptimizer minimizes quadratic function", "[utils][nelder_mead]") {
	NelderMeadOptimizer optimizer;
	
	// Minimize f(x) = (x - 5)^2, minimum at x = 5
	auto objective = [](const std::vector<double>& x) -> double {
		return (x[0] - 5.0) * (x[0] - 5.0);
	};
	
	std::vector<double> initial{0.0};
	NelderMeadOptimizer::Options options;
	options.max_iterations = 100;
	options.tolerance = 1e-6;
	
	auto result = optimizer.minimize(objective, initial, options);
	
	REQUIRE(result.converged || result.iterations > 0);
	REQUIRE(result.best[0] == Catch::Approx(5.0).margin(0.1));
	REQUIRE(result.value == Catch::Approx(0.0).margin(0.01));
}

TEST_CASE("NelderMeadOptimizer minimizes 2D function", "[utils][nelder_mead]") {
	NelderMeadOptimizer optimizer;
	
	// Minimize f(x,y) = (x-2)^2 + (y-3)^2, minimum at (2, 3)
	auto objective = [](const std::vector<double>& x) -> double {
		return (x[0] - 2.0) * (x[0] - 2.0) + (x[1] - 3.0) * (x[1] - 3.0);
	};
	
	std::vector<double> initial{0.0, 0.0};
	NelderMeadOptimizer::Options options;
	options.max_iterations = 100;
	options.tolerance = 1e-6;
	
	auto result = optimizer.minimize(objective, initial, options);
	
	REQUIRE(result.converged || result.iterations > 0);
	REQUIRE(result.best.size() == 2);
	REQUIRE(result.best[0] == Catch::Approx(2.0).margin(0.1));
	REQUIRE(result.best[1] == Catch::Approx(3.0).margin(0.1));
}

TEST_CASE("NelderMeadOptimizer respects bounds", "[utils][nelder_mead]") {
	NelderMeadOptimizer optimizer;
	
	auto objective = [](const std::vector<double>& x) -> double {
		return (x[0] - 10.0) * (x[0] - 10.0);  // Minimum at 10, but bounded
	};
	
	std::vector<double> initial{0.0};
	std::vector<double> lower{0.0};
	std::vector<double> upper{5.0};  // Constrain to [0, 5]
	
	NelderMeadOptimizer::Options options;
	options.max_iterations = 100;
	options.tolerance = 1e-6;
	
	auto result = optimizer.minimize(objective, initial, options, lower, upper);
	
	REQUIRE(result.best[0] >= 0.0);
	REQUIRE(result.best[0] <= 5.0);
	// Should find minimum within bounds (at 5.0)
	REQUIRE(result.best[0] == Catch::Approx(5.0).margin(0.1));
}

TEST_CASE("NelderMeadOptimizer handles empty initial", "[utils][nelder_mead][edge]") {
	NelderMeadOptimizer optimizer;
	
	auto objective = [](const std::vector<double>& x) -> double {
		return x[0] * x[0];
	};
	
	std::vector<double> empty;
	NelderMeadOptimizer::Options options;
	
	auto result = optimizer.minimize(objective, empty, options);
	
	REQUIRE(result.best.empty());
	REQUIRE(result.iterations == 0);
}

TEST_CASE("NelderMeadOptimizer with custom options", "[utils][nelder_mead]") {
	NelderMeadOptimizer optimizer;
	
	auto objective = [](const std::vector<double>& x) -> double {
		return x[0] * x[0] + x[1] * x[1];
	};
	
	std::vector<double> initial{10.0, 10.0};
	NelderMeadOptimizer::Options options;
	options.max_iterations = 50;
	options.tolerance = 1e-4;
	options.step = 1.0;
	options.alpha = 1.0;
	options.gamma = 2.0;
	options.rho = 0.5;
	options.sigma = 0.5;
	
	auto result = optimizer.minimize(objective, initial, options);
	
	REQUIRE(result.iterations <= 50);
	REQUIRE(result.best.size() == 2);
	REQUIRE(result.value >= 0.0);
}

TEST_CASE("NelderMeadOptimizer simplexSpread", "[utils][nelder_mead]") {
	NelderMeadOptimizer optimizer;
	
	std::vector<double> values{1.0, 2.0, 3.0, 4.0, 5.0};
	double spread = optimizer.simplexSpread(values);
	
	// Should compute standard deviation
	REQUIRE(spread > 0.0);
	REQUIRE(std::isfinite(spread));
}

TEST_CASE("NelderMeadOptimizer enforceBounds", "[utils][nelder_mead]") {
	NelderMeadOptimizer optimizer;
	
	std::vector<double> point{10.0, -5.0, 0.5};
	std::vector<double> lower{0.0, 0.0, 0.0};
	std::vector<double> upper{5.0, 5.0, 5.0};
	
	optimizer.enforceBounds(point, lower, upper);
	
	REQUIRE(point[0] == 5.0);   // Clamped to upper
	REQUIRE(point[1] == 0.0);   // Clamped to lower
	REQUIRE(point[2] == 0.5);   // Within bounds
}

TEST_CASE("NelderMeadOptimizer enforceBounds with empty bounds", "[utils][nelder_mead]") {
	NelderMeadOptimizer optimizer;
	
	std::vector<double> point{10.0, -5.0};
	std::vector<double> empty;
	
	optimizer.enforceBounds(point, empty, empty);
	
	// Should not modify point
	REQUIRE(point[0] == 10.0);
	REQUIRE(point[1] == -5.0);
}

TEST_CASE("NelderMeadOptimizer with difficult function", "[utils][nelder_mead]") {
	NelderMeadOptimizer optimizer;
	
	// Rosenbrock function (hard to optimize)
	auto rosenbrock = [](const std::vector<double>& x) -> double {
		return 100.0 * (x[1] - x[0] * x[0]) * (x[1] - x[0] * x[0]) + (1.0 - x[0]) * (1.0 - x[0]);
	};
	
	std::vector<double> initial{-1.2, 1.0};
	NelderMeadOptimizer::Options options;
	options.max_iterations = 200;
	options.tolerance = 1e-4;
	
	auto result = optimizer.minimize(rosenbrock, initial, options);
	
	REQUIRE(result.iterations > 0);
	REQUIRE(result.best.size() == 2);
	// Rosenbrock minimum is at (1, 1)
	REQUIRE(result.best[0] == Catch::Approx(1.0).margin(0.5));
	REQUIRE(result.best[1] == Catch::Approx(1.0).margin(0.5));
}

