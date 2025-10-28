#include "time_series_builder.hpp"
#include "duckdb/common/exception.hpp"
#include <iostream>
#include <algorithm>

// Need to include full types for unique_ptr destructor
#include "anofox-time/core/time_series.hpp"

namespace duckdb {

std::unique_ptr<::anofoxtime::core::TimeSeries>
TimeSeriesBuilder::BuildTimeSeries(const std::vector<std::chrono::system_clock::time_point> &timestamps,
                                   const std::vector<double> &values) {
	// std::cerr << "[DEBUG] TimeSeriesBuilder::BuildTimeSeries (vector version) with " << timestamps.size() << "
	// points" << std::endl;
	return AnofoxTimeWrapper::BuildTimeSeries(timestamps, values);
}

std::unique_ptr<::anofoxtime::core::TimeSeries> TimeSeriesBuilder::BuildTimeSeries(const DataChunk &timestamp_chunk,
                                                                                   const DataChunk &value_chunk,
                                                                                   idx_t start_idx, idx_t count) {
	// std::cerr << "[DEBUG] TimeSeriesBuilder::BuildTimeSeries called with count: " << count << std::endl;

	// Validate input data first
	ValidateTimeSeriesData(timestamp_chunk, value_chunk, start_idx, count);

	if (count == 0) {
		// std::cerr << "[DEBUG] Empty time series requested" << std::endl;
		std::vector<std::chrono::system_clock::time_point> empty_timestamps;
		std::vector<double> empty_values;
		return AnofoxTimeWrapper::BuildTimeSeries(empty_timestamps, empty_values);
	}

	// Extract timestamps and values
	std::vector<std::chrono::system_clock::time_point> timestamps;
	std::vector<double> values;

	timestamps.reserve(count);
	values.reserve(count);

	for (idx_t i = start_idx; i < start_idx + count; i++) {
		// Convert timestamp
		auto timestamp_value = timestamp_chunk.GetValue(0, i);
		auto time_point = ConvertTimestamp(timestamp_value);
		timestamps.push_back(time_point);

		// Extract value
		auto value = value_chunk.GetValue(0, i).GetValue<double>();
		values.push_back(value);
	}

	// std::cerr << "[DEBUG] Built time series with " << timestamps.size() << " points" << std::endl;

	// Validate timestamps are strictly increasing
	for (size_t i = 1; i < timestamps.size(); i++) {
		if (timestamps[i] <= timestamps[i - 1]) {
			throw InvalidInputException("Timestamps must be strictly increasing");
		}
	}

	return AnofoxTimeWrapper::BuildTimeSeries(timestamps, values);
}

void TimeSeriesBuilder::ValidateTimeSeriesData(const DataChunk &timestamp_chunk, const DataChunk &value_chunk,
                                               idx_t start_idx, idx_t count) {
	// std::cerr << "[DEBUG] Validating time series data" << std::endl;

	if (count == 0) {
		return; // Empty series is valid
	}

	if (start_idx + count > timestamp_chunk.size()) {
		throw InvalidInputException("Timestamp chunk size insufficient for requested range");
	}

	if (start_idx + count > value_chunk.size()) {
		throw InvalidInputException("Value chunk size insufficient for requested range");
	}

	// Validate timestamp column type
	if (timestamp_chunk.data[0].GetType() != LogicalType::TIMESTAMP) {
		throw InvalidInputException("Timestamp column must be of type TIMESTAMP");
	}

	// Validate value column type
	if (value_chunk.data[0].GetType() != LogicalType::DOUBLE) {
		throw InvalidInputException("Value column must be of type DOUBLE");
	}

	// Validate values are finite
	ValidateValues(value_chunk, start_idx, count);
}

std::chrono::system_clock::time_point TimeSeriesBuilder::ConvertTimestamp(const Value &timestamp_value) {
	if (timestamp_value.IsNull()) {
		throw InvalidInputException("Timestamp value cannot be NULL");
	}

	// DuckDB timestamps are stored as microseconds since epoch
	auto ts_micros = timestamp_value.GetValue<timestamp_t>();
	auto duration = std::chrono::microseconds(ts_micros.value);
	return std::chrono::system_clock::time_point(duration);
}

void TimeSeriesBuilder::ValidateValues(const DataChunk &value_chunk, idx_t start_idx, idx_t count) {
	// std::cerr << "[DEBUG] Validating " << count << " values for finiteness" << std::endl;

	for (idx_t i = start_idx; i < start_idx + count; i++) {
		auto value = value_chunk.GetValue(0, i);
		if (value.IsNull()) {
			throw InvalidInputException("Value cannot be NULL at index " + std::to_string(i));
		}

		double val = value.GetValue<double>();
		if (!IsFinite(val)) {
			throw InvalidInputException("Value at index " + std::to_string(i) +
			                            " is not finite: " + std::to_string(val));
		}
	}
}

bool TimeSeriesBuilder::IsFinite(double value) {
	return std::isfinite(value);
}

} // namespace duckdb
