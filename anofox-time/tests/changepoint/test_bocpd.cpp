#include <catch2/catch_test_macros.hpp>

#include "anofox-time/changepoint/bocpd.hpp"

#include <vector>

using anofoxtime::changepoint::BocpdDetector;

namespace {

std::vector<double> makeStepSeries(std::size_t left, std::size_t right, double left_value, double right_value) {
	std::vector<double> data;
	data.reserve(left + right);
	data.insert(data.end(), left, left_value);
	data.insert(data.end(), right, right_value);
	return data;
}

bool containsNear(const std::vector<std::size_t>& cps, std::size_t target, std::size_t tolerance) {
	for (auto cp : cps) {
		if (cp >= target ? cp - target <= tolerance : target - cp <= tolerance) {
			return true;
		}
	}
	return false;
}

} // namespace

TEST_CASE("BOCPD detects simple changepoint", "[changepoint][bocpd]") {
	const auto data = makeStepSeries(20, 20, 0.0, 5.0);
	auto detector = BocpdDetector::builder().hazardLambda(100.0).maxRunLength(50).build();
	const auto changepoints = detector.detect(data);
	REQUIRE(changepoints.front() == 0);
	REQUIRE(changepoints.back() == data.size() - 1);
	REQUIRE(containsNear(changepoints, 20, 2));
}

TEST_CASE("BOCPD handles empty input", "[changepoint][bocpd][edge]") {
	auto detector = BocpdDetector::builder().build();
	const auto changepoints = detector.detect({});
	REQUIRE(changepoints.empty());
}
