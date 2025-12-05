// Standalone test for ts_fill_gaps helper functions
// This can be compiled and run independently to verify core logic
// Compile with: g++ -std=c++17 -I. -Isrc/include -Iduckdb/src/include test/cpp/test_ts_fill_gaps_standalone.cpp
// src/ts_fill_gaps_function.cpp -o test_ts_fill_gaps_standalone

#include <iostream>
#include <chrono>
#include <vector>
#include <cassert>
#include <stdexcept>

// Minimal includes to test helper functions
#include "ts_fill_gaps_function.hpp"

using namespace duckdb;
using namespace duckdb::ts_fill_gaps_internal;
using namespace std::chrono;

// Test helpers
void test_frequency_validation() {
	std::cout << "Testing frequency validation...\n";

	// DATE + INTEGER should fail
	try {
		FrequencyConfig int_freq;
		int_freq.type = FrequencyType::INTEGER_STEP;
		int_freq.step = 1;
		ValidateFrequencyCompatibility(LogicalType::DATE, int_freq);
		std::cerr << "ERROR: DATE + INTEGER should have thrown!\n";
		exit(1);
	} catch (const InvalidInputException &e) {
		std::cout << "  ✓ DATE + INTEGER correctly rejected\n";
	}

	// DATE + "30m" should fail
	try {
		FrequencyConfig subday_freq;
		subday_freq.type = FrequencyType::VARCHAR_INTERVAL;
		subday_freq.interval = minutes(30);
		ValidateFrequencyCompatibility(LogicalType::DATE, subday_freq);
		std::cerr << "ERROR: DATE + 30m should have thrown!\n";
		exit(1);
	} catch (const InvalidInputException &e) {
		std::cout << "  ✓ DATE + 30m correctly rejected\n";
	}

	// DATE + "1d" should succeed
	FrequencyConfig day_freq;
	day_freq.type = FrequencyType::VARCHAR_INTERVAL;
	day_freq.interval = hours(24);
	ValidateFrequencyCompatibility(LogicalType::DATE, day_freq);
	std::cout << "  ✓ DATE + 1d correctly accepted\n";

	std::cout << "Frequency validation tests passed!\n\n";
}

void test_frequency_parsing() {
	std::cout << "Testing frequency parsing...\n";

	// Test interval string parsing
	auto interval_1d = ParseIntervalString("1d");
	assert(interval_1d == hours(24));
	std::cout << "  ✓ Parse '1d' = 24 hours\n";

	auto interval_1h = ParseIntervalString("1h");
	assert(interval_1h == hours(1));
	std::cout << "  ✓ Parse '1h' = 1 hour\n";

	auto interval_30m = ParseIntervalString("30m");
	assert(interval_30m == minutes(30));
	std::cout << "  ✓ Parse '30m' = 30 minutes\n";

	auto interval_1w = ParseIntervalString("1w");
	assert(interval_1w == hours(24 * 7));
	std::cout << "  ✓ Parse '1w' = 7 days\n";

	std::cout << "Frequency parsing tests passed!\n\n";
}

void test_date_range_generation() {
	std::cout << "Testing date range generation...\n";

	// Test daily range
	auto min_date = system_clock::time_point(hours(24 * 18262)); // Approx 2024-01-01
	auto max_date = system_clock::time_point(hours(24 * 18266)); // Approx 2024-01-05
	auto range = GenerateDateRange(min_date, max_date, hours(24));

	assert(range.size() == 5);
	assert(range[0] == min_date);
	assert(range[4] == max_date);
	std::cout << "  ✓ Daily range: 5 dates generated\n";

	// Test single date
	auto single_range = GenerateDateRange(min_date, min_date, hours(24));
	assert(single_range.size() == 1);
	std::cout << "  ✓ Single date: 1 date generated\n";

	// Test empty range
	auto empty_range = GenerateDateRange(max_date, min_date, hours(24));
	assert(empty_range.size() == 0);
	std::cout << "  ✓ Empty range: 0 dates generated\n";

	std::cout << "Date range generation tests passed!\n\n";
}

void test_integer_range_generation() {
	std::cout << "Testing integer range generation...\n";

	// Test step 1
	auto range1 = GenerateIntegerRange(1, 10, 1);
	assert(range1.size() == 10);
	assert(range1[0] == 1);
	assert(range1[9] == 10);
	std::cout << "  ✓ Integer range step 1: 10 values\n";

	// Test step 2
	auto range2 = GenerateIntegerRange(1, 10, 2);
	assert(range2.size() == 5);
	assert(range2[0] == 1);
	assert(range2[1] == 3);
	assert(range2[4] == 9);
	std::cout << "  ✓ Integer range step 2: 5 values\n";

	std::cout << "Integer range generation tests passed!\n\n";
}

int main() {
	std::cout << "=== ts_fill_gaps Unit Tests ===\n\n";

	try {
		test_frequency_validation();
		test_frequency_parsing();
		test_date_range_generation();
		test_integer_range_generation();

		std::cout << "=== All tests passed! ===\n";
		return 0;
	} catch (const std::exception &e) {
		std::cerr << "Test failed: " << e.what() << "\n";
		return 1;
	}
}
