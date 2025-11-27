#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include "anofox-time/features/feature_types.hpp"
#include "anofox-time/features/feature_calculators.hpp"
#include "anofox-time/features/feature_math.hpp"
#include <cmath>
#include <limits>

using namespace anofoxtime::features;
using Catch::Approx;

namespace {

// Helper to test a feature calculator
double TestFeature(const std::string &name, const Series &series) {
	auto &registry = FeatureRegistry::Instance();
	auto feature = registry.Find(name);
	if (!feature) {
		return std::numeric_limits<double>::quiet_NaN();
	}
	FeatureCache cache(series);
	return feature->calculator(series, ParameterMap {}, cache);
}

} // namespace

TEST_CASE("FeatureNNull - counts NULL values", "[features]") {
	// Note: NULLs are filtered before reaching features, so this always returns 0
	Series series = {1.0, 2.0, 3.0};
	REQUIRE(TestFeature("n_null", series) == Approx(0.0));
	
	Series empty = {};
	REQUIRE(TestFeature("n_null", empty) == Approx(0.0));
}

TEST_CASE("FeatureNZeros - counts zero values", "[features]") {
	Series no_zeros = {1.0, 2.0, 3.0, 4.0};
	REQUIRE(TestFeature("n_zeros", no_zeros) == Approx(0.0));
	
	Series some_zeros = {1.0, 0.0, 2.0, 0.0, 0.0, 3.0};
	REQUIRE(TestFeature("n_zeros", some_zeros) == Approx(3.0));
	
	Series all_zeros = {0.0, 0.0, 0.0, 0.0};
	REQUIRE(TestFeature("n_zeros", all_zeros) == Approx(4.0));
	
	Series empty = {};
	REQUIRE(TestFeature("n_zeros", empty) == Approx(0.0));
	
	Series mixed = {0.0, 1.0, 0.0, 2.0, 0.0};
	REQUIRE(TestFeature("n_zeros", mixed) == Approx(3.0));
}

TEST_CASE("FeatureNUniqueValues - counts distinct values", "[features]") {
	Series constant = {5.0, 5.0, 5.0, 5.0};
	REQUIRE(TestFeature("n_unique_values", constant) == Approx(1.0));
	
	Series all_unique = {1.0, 2.0, 3.0, 4.0, 5.0};
	REQUIRE(TestFeature("n_unique_values", all_unique) == Approx(5.0));
	
	Series some_duplicates = {1.0, 2.0, 1.0, 3.0, 2.0, 4.0};
	REQUIRE(TestFeature("n_unique_values", some_duplicates) == Approx(4.0));
	
	Series empty = {};
	REQUIRE(TestFeature("n_unique_values", empty) == Approx(0.0));
	
	Series single = {42.0};
	REQUIRE(TestFeature("n_unique_values", single) == Approx(1.0));
	
	Series with_zeros = {0.0, 1.0, 0.0, 2.0, 1.0};
	REQUIRE(TestFeature("n_unique_values", with_zeros) == Approx(3.0));
}

TEST_CASE("FeatureIsConstant - checks if series is constant", "[features]") {
	Series constant = {5.0, 5.0, 5.0, 5.0};
	REQUIRE(TestFeature("is_constant", constant) == Approx(1.0));
	
	Series non_constant = {1.0, 2.0, 3.0, 4.0};
	REQUIRE(TestFeature("is_constant", non_constant) == Approx(0.0));
	
	Series single = {42.0};
	REQUIRE(TestFeature("is_constant", single) == Approx(1.0));
	
	Series empty = {};
	REQUIRE(TestFeature("is_constant", empty) == Approx(1.0));
	
	Series almost_constant = {5.0, 5.0, 5.0, 5.1};
	REQUIRE(TestFeature("is_constant", almost_constant) == Approx(0.0));
	
	Series zeros = {0.0, 0.0, 0.0};
	REQUIRE(TestFeature("is_constant", zeros) == Approx(1.0));
}

TEST_CASE("FeaturePlateauSize - maximum run length of identical values", "[features]") {
	Series no_plateaus = {1.0, 2.0, 3.0, 4.0, 5.0};
	REQUIRE(TestFeature("plateau_size", no_plateaus) == Approx(1.0));
	
	Series single_plateau = {1.0, 2.0, 2.0, 2.0, 2.0, 3.0};
	REQUIRE(TestFeature("plateau_size", single_plateau) == Approx(4.0));
	
	Series multiple_plateaus = {1.0, 1.0, 2.0, 2.0, 2.0, 3.0, 3.0, 3.0, 3.0, 3.0};
	REQUIRE(TestFeature("plateau_size", multiple_plateaus) == Approx(5.0));
	
	Series constant = {5.0, 5.0, 5.0, 5.0};
	REQUIRE(TestFeature("plateau_size", constant) == Approx(4.0));
	
	Series empty = {};
	REQUIRE(TestFeature("plateau_size", empty) == Approx(0.0));
	
	Series single = {42.0};
	REQUIRE(TestFeature("plateau_size", single) == Approx(1.0));
	
	Series with_zeros = {0.0, 0.0, 0.0, 1.0, 2.0, 2.0};
	REQUIRE(TestFeature("plateau_size", with_zeros) == Approx(3.0));
}

TEST_CASE("FeaturePlateauSizeNonZero - maximum run length of identical non-zero values", "[features]") {
	Series no_nonzero_plateaus = {0.0, 0.0, 1.0, 2.0, 3.0};
	REQUIRE(TestFeature("plateau_size_non_zero", no_nonzero_plateaus) == Approx(1.0));
	
	Series long_nonzero_plateau = {0.0, 1.0, 1.0, 1.0, 1.0, 1.0, 2.0};
	REQUIRE(TestFeature("plateau_size_non_zero", long_nonzero_plateau) == Approx(5.0));
	
	Series ignores_zero_plateaus = {0.0, 0.0, 0.0, 1.0, 1.0, 2.0, 2.0, 2.0};
	REQUIRE(TestFeature("plateau_size_non_zero", ignores_zero_plateaus) == Approx(3.0));
	
	Series constant_nonzero = {5.0, 5.0, 5.0, 5.0};
	REQUIRE(TestFeature("plateau_size_non_zero", constant_nonzero) == Approx(4.0));
	
	Series all_zeros = {0.0, 0.0, 0.0, 0.0};
	REQUIRE(TestFeature("plateau_size_non_zero", all_zeros) == Approx(0.0));
	
	Series empty = {};
	REQUIRE(TestFeature("plateau_size_non_zero", empty) == Approx(0.0));
	
	Series mixed = {0.0, 0.0, 1.0, 1.0, 1.0, 0.0, 2.0, 2.0};
	REQUIRE(TestFeature("plateau_size_non_zero", mixed) == Approx(3.0));
}

TEST_CASE("FeatureNZerosStart - counts leading zeros", "[features]") {
	Series no_leading_zeros = {1.0, 2.0, 0.0, 3.0};
	REQUIRE(TestFeature("n_zeros_start", no_leading_zeros) == Approx(0.0));
	
	Series some_leading_zeros = {0.0, 0.0, 0.0, 1.0, 2.0, 3.0};
	REQUIRE(TestFeature("n_zeros_start", some_leading_zeros) == Approx(3.0));
	
	Series all_zeros = {0.0, 0.0, 0.0, 0.0};
	REQUIRE(TestFeature("n_zeros_start", all_zeros) == Approx(4.0));
	
	Series zeros_in_middle = {1.0, 0.0, 0.0, 2.0};
	REQUIRE(TestFeature("n_zeros_start", zeros_in_middle) == Approx(0.0));
	
	Series zeros_at_end = {1.0, 2.0, 0.0, 0.0};
	REQUIRE(TestFeature("n_zeros_start", zeros_at_end) == Approx(0.0));
	
	Series empty = {};
	REQUIRE(TestFeature("n_zeros_start", empty) == Approx(0.0));
	
	Series single_zero = {0.0};
	REQUIRE(TestFeature("n_zeros_start", single_zero) == Approx(1.0));
	
	Series single_nonzero = {42.0};
	REQUIRE(TestFeature("n_zeros_start", single_nonzero) == Approx(0.0));
}

TEST_CASE("FeatureNZerosEnd - counts trailing zeros", "[features]") {
	Series no_trailing_zeros = {1.0, 2.0, 0.0, 3.0};
	REQUIRE(TestFeature("n_zeros_end", no_trailing_zeros) == Approx(0.0));
	
	Series some_trailing_zeros = {1.0, 2.0, 3.0, 0.0, 0.0, 0.0};
	REQUIRE(TestFeature("n_zeros_end", some_trailing_zeros) == Approx(3.0));
	
	Series all_zeros = {0.0, 0.0, 0.0, 0.0};
	REQUIRE(TestFeature("n_zeros_end", all_zeros) == Approx(4.0));
	
	Series zeros_at_start = {0.0, 0.0, 1.0, 2.0};
	REQUIRE(TestFeature("n_zeros_end", zeros_at_start) == Approx(0.0));
	
	Series zeros_in_middle = {1.0, 0.0, 0.0, 2.0};
	REQUIRE(TestFeature("n_zeros_end", zeros_in_middle) == Approx(0.0));
	
	Series empty = {};
	REQUIRE(TestFeature("n_zeros_end", empty) == Approx(0.0));
	
	Series single_zero = {0.0};
	REQUIRE(TestFeature("n_zeros_end", single_zero) == Approx(1.0));
	
	Series single_nonzero = {42.0};
	REQUIRE(TestFeature("n_zeros_end", single_nonzero) == Approx(0.0));
	
	Series both_ends = {0.0, 0.0, 1.0, 2.0, 0.0, 0.0};
	REQUIRE(TestFeature("n_zeros_end", both_ends) == Approx(2.0));
	REQUIRE(TestFeature("n_zeros_start", both_ends) == Approx(2.0));
}

TEST_CASE("Run-length encoding edge cases", "[features]") {
	// Test that run-length encoding works correctly for various patterns
	Series alternating = {1.0, 2.0, 1.0, 2.0, 1.0, 2.0};
	REQUIRE(TestFeature("plateau_size", alternating) == Approx(1.0));
	
	Series long_series;
	for (int i = 0; i < 1000; ++i) {
		long_series.push_back(static_cast<double>(i % 10));
	}
	// Should find plateaus of length 1 (each value appears once in sequence)
	REQUIRE(TestFeature("plateau_size", long_series) == Approx(1.0));
	
	Series repeated_pattern = {1.0, 1.0, 2.0, 2.0, 3.0, 3.0, 1.0, 1.0};
	REQUIRE(TestFeature("plateau_size", repeated_pattern) == Approx(2.0));
}

TEST_CASE("Feature combinations", "[features]") {
	// Test that features work together correctly
	Series constant_zeros = {0.0, 0.0, 0.0, 0.0};
	REQUIRE(TestFeature("is_constant", constant_zeros) == Approx(1.0));
	REQUIRE(TestFeature("n_zeros", constant_zeros) == Approx(4.0));
	REQUIRE(TestFeature("n_unique_values", constant_zeros) == Approx(1.0));
	REQUIRE(TestFeature("plateau_size", constant_zeros) == Approx(4.0));
	REQUIRE(TestFeature("plateau_size_non_zero", constant_zeros) == Approx(0.0));
	REQUIRE(TestFeature("n_zeros_start", constant_zeros) == Approx(4.0));
	REQUIRE(TestFeature("n_zeros_end", constant_zeros) == Approx(4.0));
	
	Series complex = {0.0, 0.0, 5.0, 5.0, 5.0, 2.0, 0.0, 0.0};
	REQUIRE(TestFeature("is_constant", complex) == Approx(0.0));
	REQUIRE(TestFeature("n_zeros", complex) == Approx(4.0));
	REQUIRE(TestFeature("n_unique_values", complex) == Approx(3.0));
	REQUIRE(TestFeature("plateau_size", complex) == Approx(3.0));
	REQUIRE(TestFeature("plateau_size_non_zero", complex) == Approx(3.0));
	REQUIRE(TestFeature("n_zeros_start", complex) == Approx(2.0));
	REQUIRE(TestFeature("n_zeros_end", complex) == Approx(2.0));
}

