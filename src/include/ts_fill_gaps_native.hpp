#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "anofox_fcst_ffi.h"

namespace duckdb {

// Date column type enum for type preservation
enum class DateColumnType {
    DATE,
    TIMESTAMP,
    INTEGER,
    BIGINT
};

// Frequency parsing result with type information
struct ParsedFrequency {
    int64_t seconds;      // Frequency in seconds (for fixed intervals)
    bool is_raw;          // True if parsed as pure integer (for integer date columns)
    FrequencyType type;   // FIXED, MONTHLY, QUARTERLY, or YEARLY
};

// Helper functions
std::pair<int64_t, bool> ParseFrequencyToSeconds(const string &frequency_str);
ParsedFrequency ParseFrequencyWithType(const string &frequency_str);
int64_t DateToMicroseconds(date_t date);
int64_t TimestampToMicroseconds(timestamp_t ts);
date_t MicrosecondsToDate(int64_t micros);
timestamp_t MicrosecondsToTimestamp(int64_t micros);
string GetGroupKey(const Value &group_value);

// Registration functions
void RegisterTsFillGapsNativeFunction(ExtensionLoader &loader);
void RegisterTsFillForwardNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
