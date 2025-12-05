#define CATCH_CONFIG_MAIN
#include "duckdb.hpp"
#include "duckdb/third_party/catch/catch.hpp"
#include "duckdb/common/types/value.hpp"
#include "anofox-time/seasonality/mstl.hpp"
#include "anofox-time/core/time_series.hpp"
#include "mstl_decomposition_function.hpp"
#include <vector>
#include <cmath>
#include <numeric>

using namespace anofoxtime::core;
using namespace anofoxtime::seasonality;
using namespace duckdb;

// Helper to create TimeSeries
TimeSeries CreateTimeSeries(size_t length, const std::vector<double> &values) {
	std::vector<TimeSeries::TimePoint> timestamps;
	timestamps.reserve(length);
	auto start = std::chrono::system_clock::now();
	for (size_t i = 0; i < length; i++) {
		timestamps.push_back(start + std::chrono::hours(24 * i));
	}
	return TimeSeries(timestamps, values);
}

TEST_CASE("MSTL Decomposition - Additivity", "[mstl]") {
	size_t n = 100;
	std::vector<double> values(n);
	for (size_t i = 0; i < n; i++) {
		values[i] = 10.0 + 0.1 * i + std::sin(2.0 * 3.14159 * i / 7.0); // Linear trend + seasonal 7
	}
	TimeSeries ts = CreateTimeSeries(n, values);

	// Use periods = {7} for this test, similar to default if we used builder
	MSTLDecomposition decomposer({7}, 2, false);
	decomposer.fit(ts);

	// Verify additivity: Observed = Trend + Seasonal... + Residual
	const auto &comps = decomposer.components();
	const auto &trend = comps.trend;
	const auto &seasonal = comps.seasonal;
	const auto &residual = comps.remainder;

	REQUIRE(trend.size() == n);
	REQUIRE(residual.size() == n);
	REQUIRE(seasonal.size() >= 1);

	for (size_t i = 0; i < n; i++) {
		double sum = trend[i] + residual[i];
		for (const auto &s : seasonal) {
			sum += s[i];
		}
		REQUIRE(std::abs(values[i] - sum) < 1e-5);
	}
}

TEST_CASE("MSTL Decomposition - Seasonal Periods", "[mstl]") {
	size_t n = 100;
	std::vector<double> values(n);
	for (size_t i = 0; i < n; i++) {
		values[i] = std::sin(2.0 * 3.14159 * i / 7.0) + std::sin(2.0 * 3.14159 * i / 14.0);
	}
	TimeSeries ts = CreateTimeSeries(n, values);

	MSTLDecomposition decomposer({7, 14}, 2, false);
	decomposer.fit(ts);

	const auto &comps = decomposer.components();
	REQUIRE(comps.seasonal.size() == 2);
}

TEST_CASE("ExtractSeasonalPeriods", "[mstl_helpers]") {
	// Note: TSMstlExtractPeriods now expects a MAP/STRUCT with "seasonal_periods" key!

	// Test INT list inside STRUCT
	duckdb::vector<Value> list_vals;
	list_vals.push_back(Value(7));
	list_vals.push_back(Value(365));
	Value params_list = Value::LIST(LogicalType::INTEGER, list_vals);

	// Test STRUCT param
	child_list_t<Value> struct_vals;
	struct_vals.push_back(make_pair("seasonal_periods", params_list));
	Value params_struct = Value::STRUCT(struct_vals);

	auto periods1 = duckdb::TSMstlExtractPeriods(params_struct);
	REQUIRE(periods1.size() == 2);
	REQUIRE(periods1[0] == 7);

	// Test MAP param
	duckdb::vector<Value> keys;
	keys.push_back(Value("seasonal_periods"));

	duckdb::vector<Value> values_vec;
	values_vec.push_back(Value::LIST(LogicalType::INTEGER, {Value(12)}));

	Value params_map = Value::MAP(LogicalType::VARCHAR, LogicalType::LIST(LogicalType::INTEGER), keys, values_vec);

	auto periods2 = duckdb::TSMstlExtractPeriods(params_map);
	REQUIRE(periods2.size() == 1);
	REQUIRE(periods2[0] == 12);

	// Test NULL/Empty - Should THROW or be empty? Implementation throws if "seasonal_periods" missing.
	Value empty_params; // Null
	// REQUIRE_THROWS_AS(duckdb::TSMstlExtractPeriods(empty_params), InvalidInputException);
	// But let's check what happens if valid param object but missing key.

	// Empty MAP
	duckdb::vector<Value> empty_keys;
	duckdb::vector<Value> empty_vals;
	Value empty_map = Value::MAP(LogicalType::VARCHAR, LogicalType::INTEGER, empty_keys, empty_vals);

	REQUIRE_THROWS(duckdb::TSMstlExtractPeriods(empty_map));
}
