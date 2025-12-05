#define CATCH_CONFIG_MAIN
// Use DuckDB's catch.hpp for compatibility with DuckDB types
#include "duckdb/third_party/catch/catch.hpp"
#include "duckdb.hpp"

#include "ts_fill_gaps_function.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include <chrono>
#include <vector>

using namespace duckdb;
using namespace duckdb::ts_fill_gaps_internal;
using namespace std::chrono;

// Helper to create LogicalType for testing
LogicalType CreateDateType() {
	return LogicalType::DATE;
}

LogicalType CreateTimestampType() {
	return LogicalType::TIMESTAMP;
}

LogicalType CreateIntegerType() {
	return LogicalType::INTEGER;
}

LogicalType CreateBigIntType() {
	return LogicalType::BIGINT;
}

// Helper to create Value for testing
Value CreateVarcharValue(const std::string &str) {
	return Value(str);
}

Value CreateIntegerValue(int64_t val) {
	return Value(val);
}

// Test frequency validation
TEST_CASE("Frequency Validation - DATE column restrictions", "[validation]") {
	auto date_type = CreateDateType();

	// DATE + INTEGER frequency should fail
	FrequencyConfig int_freq;
	int_freq.type = FrequencyType::INTEGER_STEP;
	int_freq.step = 1;
	REQUIRE_THROWS_AS(ValidateFrequencyCompatibility(date_type, int_freq), InvalidInputException);

	// DATE + "30m" should fail
	FrequencyConfig subday_freq;
	subday_freq.type = FrequencyType::VARCHAR_INTERVAL;
	subday_freq.interval = minutes(30);
	REQUIRE_THROWS_AS(ValidateFrequencyCompatibility(date_type, subday_freq), InvalidInputException);

	// DATE + "1h" should fail
	subday_freq.interval = hours(1);
	REQUIRE_THROWS_AS(ValidateFrequencyCompatibility(date_type, subday_freq), InvalidInputException);

	// DATE + "1d" should succeed
	FrequencyConfig day_freq;
	day_freq.type = FrequencyType::VARCHAR_INTERVAL;
	day_freq.interval = hours(24);
	REQUIRE_NOTHROW(ValidateFrequencyCompatibility(date_type, day_freq));
}

TEST_CASE("Frequency Validation - TIMESTAMP column", "[validation]") {
	auto timestamp_type = CreateTimestampType();

	// TIMESTAMP + INTEGER frequency should succeed
	FrequencyConfig int_freq;
	int_freq.type = FrequencyType::INTEGER_STEP;
	int_freq.step = 1;
	REQUIRE_NOTHROW(ValidateFrequencyCompatibility(timestamp_type, int_freq));

	// TIMESTAMP + "30m" should succeed
	FrequencyConfig subday_freq;
	subday_freq.type = FrequencyType::VARCHAR_INTERVAL;
	subday_freq.interval = minutes(30);
	REQUIRE_NOTHROW(ValidateFrequencyCompatibility(timestamp_type, subday_freq));

	// TIMESTAMP + "1h" should succeed
	subday_freq.interval = hours(1);
	REQUIRE_NOTHROW(ValidateFrequencyCompatibility(timestamp_type, subday_freq));
}

TEST_CASE("Frequency Validation - INTEGER column", "[validation]") {
	auto int_type = CreateIntegerType();

	// INTEGER + VARCHAR frequency should fail
	FrequencyConfig varchar_freq;
	varchar_freq.type = FrequencyType::VARCHAR_INTERVAL;
	varchar_freq.interval = hours(24);
	REQUIRE_THROWS_AS(ValidateFrequencyCompatibility(int_type, varchar_freq), InvalidInputException);

	// INTEGER + INTEGER frequency should succeed
	FrequencyConfig int_freq;
	int_freq.type = FrequencyType::INTEGER_STEP;
	int_freq.step = 1;
	REQUIRE_NOTHROW(ValidateFrequencyCompatibility(int_type, int_freq));
}

TEST_CASE("Frequency Validation - BIGINT column", "[validation]") {
	auto bigint_type = CreateBigIntType();

	// BIGINT + VARCHAR frequency should fail
	FrequencyConfig varchar_freq;
	varchar_freq.type = FrequencyType::VARCHAR_INTERVAL;
	varchar_freq.interval = hours(24);
	REQUIRE_THROWS_AS(ValidateFrequencyCompatibility(bigint_type, varchar_freq), InvalidInputException);

	// BIGINT + INTEGER frequency should succeed
	FrequencyConfig int_freq;
	int_freq.type = FrequencyType::INTEGER_STEP;
	int_freq.step = 1;
	REQUIRE_NOTHROW(ValidateFrequencyCompatibility(bigint_type, int_freq));
}

// Test frequency parsing
TEST_CASE("Frequency Parsing - VARCHAR intervals", "[parsing]") {
	auto timestamp_type = CreateTimestampType();

	// Parse "1d"
	auto freq1d = ParseFrequency(CreateVarcharValue("1d"), timestamp_type);
	REQUIRE(freq1d.type == FrequencyType::VARCHAR_INTERVAL);
	REQUIRE(freq1d.interval == hours(24));

	// Parse "1h"
	auto freq1h = ParseFrequency(CreateVarcharValue("1h"), timestamp_type);
	REQUIRE(freq1h.type == FrequencyType::VARCHAR_INTERVAL);
	REQUIRE(freq1h.interval == hours(1));

	// Parse "30m"
	auto freq30m = ParseFrequency(CreateVarcharValue("30m"), timestamp_type);
	REQUIRE(freq30m.type == FrequencyType::VARCHAR_INTERVAL);
	REQUIRE(freq30m.interval == minutes(30));

	// Parse "1w"
	auto freq1w = ParseFrequency(CreateVarcharValue("1w"), timestamp_type);
	REQUIRE(freq1w.type == FrequencyType::VARCHAR_INTERVAL);
	REQUIRE(freq1w.interval == hours(24 * 7));

	// Parse "1mo"
	auto freq1mo = ParseFrequency(CreateVarcharValue("1mo"), timestamp_type);
	REQUIRE(freq1mo.type == FrequencyType::VARCHAR_INTERVAL);
	REQUIRE(freq1mo.interval == hours(24 * 30));

	// Parse "1q"
	auto freq1q = ParseFrequency(CreateVarcharValue("1q"), timestamp_type);
	REQUIRE(freq1q.type == FrequencyType::VARCHAR_INTERVAL);
	REQUIRE(freq1q.interval == hours(24 * 90));

	// Parse "1y"
	auto freq1y = ParseFrequency(CreateVarcharValue("1y"), timestamp_type);
	REQUIRE(freq1y.type == FrequencyType::VARCHAR_INTERVAL);
	REQUIRE(freq1y.interval == hours(24 * 365));
}

TEST_CASE("Frequency Parsing - INTEGER steps", "[parsing]") {
	auto int_type = CreateIntegerType();

	// Parse INTEGER 1
	auto freq1 = ParseFrequency(CreateIntegerValue(1), int_type);
	REQUIRE(freq1.type == FrequencyType::INTEGER_STEP);
	REQUIRE(freq1.step == 1);

	// Parse INTEGER 2
	auto freq2 = ParseFrequency(CreateIntegerValue(2), int_type);
	REQUIRE(freq2.type == FrequencyType::INTEGER_STEP);
	REQUIRE(freq2.step == 2);

	// Parse INTEGER 3
	auto freq3 = ParseFrequency(CreateIntegerValue(3), int_type);
	REQUIRE(freq3.type == FrequencyType::INTEGER_STEP);
	REQUIRE(freq3.step == 3);
}

TEST_CASE("Frequency Parsing - Case insensitive", "[parsing]") {
	auto timestamp_type = CreateTimestampType();

	// Test various case combinations
	REQUIRE_NOTHROW(ParseFrequency(CreateVarcharValue("1D"), timestamp_type));
	REQUIRE_NOTHROW(ParseFrequency(CreateVarcharValue("1DAY"), timestamp_type));
	REQUIRE_NOTHROW(ParseFrequency(CreateVarcharValue("1d"), timestamp_type));
	REQUIRE_NOTHROW(ParseFrequency(CreateVarcharValue("1day"), timestamp_type));
}

TEST_CASE("Frequency Parsing - DATE column validation", "[parsing]") {
	auto date_type = CreateDateType();

	// DATE + INTEGER should fail
	REQUIRE_THROWS_AS(ParseFrequency(CreateIntegerValue(1), date_type), InvalidInputException);

	// DATE + "30m" should fail
	REQUIRE_THROWS_AS(ParseFrequency(CreateVarcharValue("30m"), date_type), InvalidInputException);

	// DATE + "1h" should fail
	REQUIRE_THROWS_AS(ParseFrequency(CreateVarcharValue("1h"), date_type), InvalidInputException);

	// DATE + "1d" should succeed
	REQUIRE_NOTHROW(ParseFrequency(CreateVarcharValue("1d"), date_type));
}

// Test date range generation
TEST_CASE("Date Range Generation - Daily range", "[range]") {
	auto min_date = system_clock::time_point(hours(24 * 18262)); // 2024-01-01 (approximate)
	auto max_date = system_clock::time_point(hours(24 * 18266)); // 2024-01-05 (approximate)
	auto interval = hours(24);

	auto range = GenerateDateRange(min_date, max_date, interval);
	REQUIRE(range.size() == 5);
	REQUIRE(range[0] == min_date);
	REQUIRE(range[4] == max_date);
}

TEST_CASE("Date Range Generation - Hourly range", "[range]") {
	auto min_date = system_clock::time_point(hours(24 * 18262));             // 2024-01-01 00:00
	auto max_date = system_clock::time_point(hours(24 * 18262) + hours(23)); // 2024-01-01 23:00
	auto interval = hours(1);

	auto range = GenerateDateRange(min_date, max_date, interval);
	REQUIRE(range.size() == 24);
	REQUIRE(range[0] == min_date);
	REQUIRE(range[23] == max_date);
}

TEST_CASE("Date Range Generation - Single date", "[range]") {
	auto min_date = system_clock::time_point(hours(24 * 18262));
	auto max_date = min_date;
	auto interval = hours(24);

	auto range = GenerateDateRange(min_date, max_date, interval);
	REQUIRE(range.size() == 1);
	REQUIRE(range[0] == min_date);
}

TEST_CASE("Date Range Generation - Empty range (min > max)", "[range]") {
	auto min_date = system_clock::time_point(hours(24 * 18266));
	auto max_date = system_clock::time_point(hours(24 * 18262));
	auto interval = hours(24);

	auto range = GenerateDateRange(min_date, max_date, interval);
	REQUIRE(range.size() == 0);
}

// Test integer range generation
TEST_CASE("Integer Range Generation - Step 1", "[range]") {
	auto range = GenerateIntegerRange(1, 10, 1);
	REQUIRE(range.size() == 10);
	REQUIRE(range[0] == 1);
	REQUIRE(range[9] == 10);
}

TEST_CASE("Integer Range Generation - Step 2", "[range]") {
	auto range = GenerateIntegerRange(1, 10, 2);
	REQUIRE(range.size() == 5);
	REQUIRE(range[0] == 1);
	REQUIRE(range[1] == 3);
	REQUIRE(range[2] == 5);
	REQUIRE(range[3] == 7);
	REQUIRE(range[4] == 9);
}

TEST_CASE("Integer Range Generation - Single value", "[range]") {
	auto range = GenerateIntegerRange(5, 5, 1);
	REQUIRE(range.size() == 1);
	REQUIRE(range[0] == 5);
}

TEST_CASE("Integer Range Generation - Empty range (min > max)", "[range]") {
	auto range = GenerateIntegerRange(10, 1, 1);
	REQUIRE(range.size() == 0);
}
