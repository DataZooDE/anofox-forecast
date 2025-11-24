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

TEST_CASE("BOCPD detects multiple changepoints", "[changepoint][bocpd]") {
	std::vector<double> data;
	data.insert(data.end(), 15, 0.0);
	data.insert(data.end(), 15, 5.0);
	data.insert(data.end(), 15, 2.0);
	
	auto detector = BocpdDetector::builder().hazardLambda(100.0).maxRunLength(50).build();
	const auto changepoints = detector.detect(data);
	
	REQUIRE(changepoints.front() == 0);
	REQUIRE(changepoints.back() == data.size() - 1);
	REQUIRE(containsNear(changepoints, 15, 2));
	REQUIRE(containsNear(changepoints, 30, 2));
}

TEST_CASE("BOCPD with different hazard lambda", "[changepoint][bocpd]") {
	const auto data = makeStepSeries(20, 20, 0.0, 5.0);
	
	// Low hazard lambda (expects fewer changepoints)
	auto detector_low = BocpdDetector::builder().hazardLambda(10.0).maxRunLength(50).build();
	const auto cps_low = detector_low.detect(data);
	
	// High hazard lambda (expects more changepoints)
	auto detector_high = BocpdDetector::builder().hazardLambda(200.0).maxRunLength(50).build();
	const auto cps_high = detector_high.detect(data);
	
	REQUIRE(cps_low.size() <= cps_high.size());
}

TEST_CASE("BOCPD with short series", "[changepoint][bocpd][edge]") {
	std::vector<double> data{1.0, 1.0, 5.0, 5.0};
	auto detector = BocpdDetector::builder().hazardLambda(100.0).maxRunLength(10).build();
	const auto changepoints = detector.detect(data);
	
	REQUIRE(changepoints.front() == 0);
	REQUIRE(changepoints.back() == data.size() - 1);
}

TEST_CASE("BOCPD with constant series", "[changepoint][bocpd][edge]") {
	std::vector<double> data(20, 5.0);
	auto detector = BocpdDetector::builder().hazardLambda(100.0).maxRunLength(50).build();
	const auto changepoints = detector.detect(data);
	
	// Should still return boundaries
	REQUIRE(changepoints.front() == 0);
	REQUIRE(changepoints.back() == data.size() - 1);
}

TEST_CASE("BOCPD with single value", "[changepoint][bocpd][edge]") {
	std::vector<double> data{5.0};
	auto detector = BocpdDetector::builder().build();
	const auto changepoints = detector.detect(data);
	
	REQUIRE(changepoints.size() >= 1);
	REQUIRE(changepoints.front() == 0);
}

TEST_CASE("BOCPD with maxRunLength limit", "[changepoint][bocpd]") {
	const auto data = makeStepSeries(20, 20, 0.0, 5.0);
	auto detector = BocpdDetector::builder()
		.hazardLambda(100.0)
		.maxRunLength(10)  // Smaller than series length
		.build();
	
	const auto changepoints = detector.detect(data);
	REQUIRE(changepoints.front() == 0);
	REQUIRE(changepoints.back() == data.size() - 1);
}

TEST_CASE("BOCPD builder configuration", "[changepoint][bocpd]") {
	auto detector1 = BocpdDetector::builder().build();
	auto detector2 = BocpdDetector::builder()
		.hazardLambda(50.0)
		.maxRunLength(30)
		.build();
	
	// Both should work
	std::vector<double> data{1, 2, 3, 10, 11, 12};
	const auto cps1 = detector1.detect(data);
	const auto cps2 = detector2.detect(data);
	
	REQUIRE_FALSE(cps1.empty());
	REQUIRE_FALSE(cps2.empty());
}
