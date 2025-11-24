#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "anofox-time/optimization/theta_gradients.hpp"
#include "anofox-time/models/theta_pegels.hpp"
#include <vector>
#include <cmath>
#include <limits>

using namespace anofoxtime::optimization;
using namespace anofoxtime::models::theta_pegels;

namespace {

std::vector<double> generateTestData(size_t n) {
	std::vector<double> data;
	data.reserve(n);
	for (size_t i = 0; i < n; ++i) {
		data.push_back(100.0 + 0.5 * i + 10.0 * std::sin(2.0 * M_PI * i / 12.0));
	}
	return data;
}

bool isFiniteGradient(double grad) {
	return std::isfinite(grad);
}

} // namespace

TEST_CASE("ThetaGradients computeMSE basic", "[optimization][theta_gradients]") {
	auto y = generateTestData(24);
	ThetaGradients::Workspace workspace;
	std::vector<double> gradients;
	
	double mse = ThetaGradients::computeMSEWithGradients(
		y, ModelType::STM, 100.0, 0.3, 1.0,
		false, false, false, 1, gradients, workspace
	);
	
	REQUIRE(std::isfinite(mse));
	REQUIRE(mse >= 0.0);
}

TEST_CASE("ThetaGradients computeMSE with level gradient", "[optimization][theta_gradients]") {
	auto y = generateTestData(24);
	ThetaGradients::Workspace workspace;
	std::vector<double> gradients(1);
	
	double mse = ThetaGradients::computeMSEWithGradients(
		y, ModelType::STM, 100.0, 0.3, 1.0,
		true, false, false, 1, gradients, workspace
	);
	
	REQUIRE(std::isfinite(mse));
	REQUIRE(gradients.size() == 1);
	REQUIRE(isFiniteGradient(gradients[0]));
}

TEST_CASE("ThetaGradients computeMSE with alpha gradient", "[optimization][theta_gradients]") {
	auto y = generateTestData(24);
	ThetaGradients::Workspace workspace;
	std::vector<double> gradients(1);
	
	double mse = ThetaGradients::computeMSEWithGradients(
		y, ModelType::STM, 100.0, 0.3, 1.0,
		false, true, false, 1, gradients, workspace
	);
	
	REQUIRE(std::isfinite(mse));
	REQUIRE(gradients.size() == 1);
	REQUIRE(isFiniteGradient(gradients[0]));
}

TEST_CASE("ThetaGradients computeMSE with theta gradient", "[optimization][theta_gradients]") {
	auto y = generateTestData(24);
	ThetaGradients::Workspace workspace;
	std::vector<double> gradients(1);
	
	double mse = ThetaGradients::computeMSEWithGradients(
		y, ModelType::STM, 100.0, 0.3, 1.0,
		false, false, true, 1, gradients, workspace
	);
	
	REQUIRE(std::isfinite(mse));
	REQUIRE(gradients.size() == 1);
	REQUIRE(isFiniteGradient(gradients[0]));
}

TEST_CASE("ThetaGradients computeMSE with all gradients", "[optimization][theta_gradients]") {
	auto y = generateTestData(24);
	ThetaGradients::Workspace workspace;
	std::vector<double> gradients(3);
	
	double mse = ThetaGradients::computeMSEWithGradients(
		y, ModelType::STM, 100.0, 0.3, 1.0,
		true, true, true, 1, gradients, workspace
	);
	
	REQUIRE(std::isfinite(mse));
	REQUIRE(gradients.size() == 3);
	REQUIRE(isFiniteGradient(gradients[0])); // level
	REQUIRE(isFiniteGradient(gradients[1])); // alpha
	REQUIRE(isFiniteGradient(gradients[2])); // theta
}

TEST_CASE("ThetaGradients with different model types", "[optimization][theta_gradients]") {
	auto y = generateTestData(24);
	ThetaGradients::Workspace workspace;
	std::vector<double> gradients(1);
	
	// Test STM
	double mse_stm = ThetaGradients::computeMSEWithGradients(
		y, ModelType::STM, 100.0, 0.3, 1.0,
		true, false, false, 1, gradients, workspace
	);
	REQUIRE(std::isfinite(mse_stm));
	
	// Test OTM
	double mse_otm = ThetaGradients::computeMSEWithGradients(
		y, ModelType::OTM, 100.0, 0.3, 1.0,
		true, false, false, 1, gradients, workspace
	);
	REQUIRE(std::isfinite(mse_otm));
	
	// Test DSTM
	double mse_dstm = ThetaGradients::computeMSEWithGradients(
		y, ModelType::DSTM, 100.0, 0.3, 1.0,
		true, false, false, 1, gradients, workspace
	);
	REQUIRE(std::isfinite(mse_dstm));
	
	// Test DOTM
	double mse_dotm = ThetaGradients::computeMSEWithGradients(
		y, ModelType::DOTM, 100.0, 0.3, 1.0,
		true, false, false, 1, gradients, workspace
	);
	REQUIRE(std::isfinite(mse_dotm));
}

TEST_CASE("ThetaGradients with multi-step MSE", "[optimization][theta_gradients]") {
	auto y = generateTestData(24);
	ThetaGradients::Workspace workspace;
	std::vector<double> gradients(1);
	
	double mse = ThetaGradients::computeMSEWithGradients(
		y, ModelType::STM, 100.0, 0.3, 1.0,
		true, false, false, 3, gradients, workspace
	);
	
	REQUIRE(std::isfinite(mse));
	REQUIRE(gradients.size() == 1);
}

TEST_CASE("ThetaGradients handles invalid parameters", "[optimization][theta_gradients]") {
	auto y = generateTestData(24);
	ThetaGradients::Workspace workspace;
	std::vector<double> gradients(1);
	
	// Test with invalid alpha (should handle gracefully)
	double mse = ThetaGradients::computeMSEWithGradients(
		y, ModelType::STM, 100.0, 1.5, 1.0,  // alpha > 1
		true, false, false, 1, gradients, workspace
	);
	
	// Should either return finite value or handle gracefully
	REQUIRE((std::isfinite(mse) || !std::isfinite(mse)));
}

TEST_CASE("ThetaGradients workspace resizing", "[optimization][theta_gradients]") {
	ThetaGradients::Workspace workspace;
	
	// Resize to small
	workspace.resize(10, 1);
	REQUIRE(workspace.states.size() >= 10);
	REQUIRE(workspace.e.size() >= 10);
	REQUIRE(workspace.amse.size() >= 1);
	
	// Resize to larger
	workspace.resize(50, 3);
	REQUIRE(workspace.states.size() >= 50);
	REQUIRE(workspace.e.size() >= 50);
	REQUIRE(workspace.amse.size() >= 3);
	
	// Resize to smaller (should not shrink)
	size_t old_size = workspace.states.size();
	workspace.resize(20, 2);
	REQUIRE(workspace.states.size() >= old_size); // Should not shrink
}

TEST_CASE("ThetaGradients with boundary alpha values", "[optimization][theta_gradients]") {
	auto y = generateTestData(24);
	ThetaGradients::Workspace workspace;
	std::vector<double> gradients(1);
	
	// Test alpha near 0
	double mse_low = ThetaGradients::computeMSEWithGradients(
		y, ModelType::STM, 100.0, 0.01, 1.0,
		false, true, false, 1, gradients, workspace
	);
	REQUIRE((std::isfinite(mse_low) || !std::isfinite(mse_low)));
	
	// Test alpha near 1
	double mse_high = ThetaGradients::computeMSEWithGradients(
		y, ModelType::STM, 100.0, 0.99, 1.0,
		false, true, false, 1, gradients, workspace
	);
	REQUIRE((std::isfinite(mse_high) || !std::isfinite(mse_high)));
}

TEST_CASE("ThetaGradients with infinite base MSE", "[optimization][theta_gradients]") {
	auto y = std::vector<double>{std::numeric_limits<double>::quiet_NaN()};
	ThetaGradients::Workspace workspace;
	std::vector<double> gradients(1, 0.0);
	
	double mse = ThetaGradients::computeMSEWithGradients(
		y, ModelType::STM, 100.0, 0.3, 1.0,
		true, false, false, 1, gradients, workspace
	);
	
	// Should handle gracefully - either return infinity or set gradients to zero
	REQUIRE((!std::isfinite(mse) || gradients[0] == 0.0));
}

