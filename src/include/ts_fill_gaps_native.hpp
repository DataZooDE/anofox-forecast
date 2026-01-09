#pragma once

#include "duckdb.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/common/types/date.hpp"
#include "duckdb/common/types/timestamp.hpp"

namespace duckdb {

// Date column type enum for type preservation
enum class DateColumnType {
    DATE,
    TIMESTAMP,
    INTEGER,
    BIGINT
};

// Helper functions
std::pair<int64_t, bool> ParseFrequencyToSeconds(const string &frequency_str);
int64_t DateToMicroseconds(date_t date);
int64_t TimestampToMicroseconds(timestamp_t ts);
date_t MicrosecondsToDate(int64_t micros);
timestamp_t MicrosecondsToTimestamp(int64_t micros);
string GetGroupKey(const Value &group_value);

// Registration functions
void RegisterTsFillGapsNativeFunction(ExtensionLoader &loader);
void RegisterTsFillForwardNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
