#pragma once

#include "duckdb.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "anofox_time_wrapper.hpp"
#include <vector>
#include <chrono>

namespace duckdb {

class TimeSeriesBuilder {
public:
    // Convert vectors to anofoxtime::core::TimeSeries
    static std::unique_ptr<::anofoxtime::core::TimeSeries> BuildTimeSeries(
        const std::vector<std::chrono::system_clock::time_point>& timestamps,
        const std::vector<double>& values
    );
    
    // Convert DuckDB DataChunk to anofoxtime::core::TimeSeries
    static std::unique_ptr<::anofoxtime::core::TimeSeries> BuildTimeSeries(
        const DataChunk& timestamp_chunk,
        const DataChunk& value_chunk,
        idx_t start_idx,
        idx_t count
    );
    
    // Validate time series data
    static void ValidateTimeSeriesData(
        const DataChunk& timestamp_chunk,
        const DataChunk& value_chunk,
        idx_t start_idx,
        idx_t count
    );
    
    // Convert DuckDB timestamp to TimePoint
    static std::chrono::system_clock::time_point ConvertTimestamp(const Value& timestamp_value);
    
    // Validate that values are finite
    static void ValidateValues(const DataChunk& value_chunk, idx_t start_idx, idx_t count);

private:
    // Helper to check if a value is finite
    static bool IsFinite(double value);
};

} // namespace duckdb