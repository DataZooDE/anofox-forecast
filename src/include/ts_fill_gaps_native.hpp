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

// Frequency parsed to (microseconds, type, period_count) for calendar-aware gap
// counting/filling (CR-01 fix, 13-03 deferred item): `period_count` is the
// user-declared calendar multiplier (e.g. `2` for '2mo', `3` for '3q', `5` for
// '5y'; `1` for the single-unit '1mo'/'1q'/'1y' strings) -- the SAME value
// `ParsedFrequency::seconds` already carries for calendar (Monthly/Quarterly/
// Yearly) frequency types. `micros` uses an approximate 30/90/365-day-per-unit
// encoding, retained only for the `Fixed`-type auto-detect fallback; every
// calendar-frequency consumer (ts_stats, ts_data_quality, ts_fill_gaps_native,
// ts_fill_forward_native) must pass `period_count` (not `micros`) to Rust so
// Monthly/Quarterly/Yearly gap counting and gap filling honor the declared
// multiplier instead of silently behaving like the single-unit frequency.
struct ParsedFrequencyWithMicros {
    int64_t micros;
    FrequencyType type;
    int64_t period_count;
};

// Helper functions
std::pair<int64_t, bool> ParseFrequencyToSeconds(const string &frequency_str);
ParsedFrequency ParseFrequencyWithType(const string &frequency_str);

// Shared frequency-to-(micros,type,period_count) parser for ts_stats.cpp and
// ts_data_quality.cpp (replaces the previously-duplicated
// ParseFrequencyForStats/ParseFrequencyForDataQuality -- WR-02).
ParsedFrequencyWithMicros ParseFrequencyToMicrosAndType(const string &frequency_str);
int64_t DateToMicroseconds(date_t date);
int64_t TimestampToMicroseconds(timestamp_t ts);
date_t MicrosecondsToDate(int64_t micros);
timestamp_t MicrosecondsToTimestamp(int64_t micros);
string GetGroupKey(const Value &group_value);

// Registration functions
void RegisterTsFillGapsNativeFunction(ExtensionLoader &loader);
void RegisterTsFillForwardNativeFunction(ExtensionLoader &loader);

} // namespace duckdb
