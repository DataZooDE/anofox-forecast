#include "ts_fill_gaps_native.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <regex>
#include <cmath>
#include <map>

namespace duckdb {

// ============================================================================
// Helper Functions
// ============================================================================

// Returns (frequency_seconds, is_raw)
// is_raw=true means the input was a pure integer with no unit
std::pair<int64_t, bool> ParseFrequencyToSeconds(const string &frequency_str) {
    string upper = StringUtil::Upper(frequency_str);
    StringUtil::Trim(upper);

    // Try to parse Polars-style frequency (e.g., "1d", "1h", "30m")
    std::regex polars_regex("^([0-9]+)(d|h|m|min|w|mo|q|y)$", std::regex::icase);
    std::smatch match;

    if (std::regex_match(upper, match, polars_regex)) {
        int64_t count = std::stoll(match[1].str());
        string unit = StringUtil::Lower(match[2].str());

        if (unit == "d") return {count * 86400, false};
        if (unit == "h") return {count * 3600, false};
        if (unit == "m" || unit == "min") return {count * 60, false};
        if (unit == "w") return {count * 86400 * 7, false};
        if (unit == "mo") return {count * 86400 * 30, false};
        if (unit == "q") return {count * 86400 * 90, false};
        if (unit == "y") return {count * 86400 * 365, false};
    }

    // Try to parse DuckDB INTERVAL style (e.g., "1 day", "1 hour")
    std::regex interval_regex("^([0-9]+)\\s*(day|days|hour|hours|minute|minutes|week|weeks|month|months|year|years)$", std::regex::icase);

    if (std::regex_match(upper, match, interval_regex)) {
        int64_t count = std::stoll(match[1].str());
        string unit = StringUtil::Lower(match[2].str());

        if (unit == "day" || unit == "days") return {count * 86400, false};
        if (unit == "hour" || unit == "hours") return {count * 3600, false};
        if (unit == "minute" || unit == "minutes") return {count * 60, false};
        if (unit == "week" || unit == "weeks") return {count * 86400 * 7, false};
        if (unit == "month" || unit == "months") return {count * 86400 * 30, false};
        if (unit == "year" || unit == "years") return {count * 86400 * 365, false};
    }

    // Try to parse as pure integer - mark as raw for integer date columns
    std::regex int_regex("^[0-9]+$");
    if (std::regex_match(upper, int_regex)) {
        // For pure integers, store the raw value. The caller will interpret based on date type.
        return {std::stoll(upper), true};
    }

    // Default to 1 day
    return {86400, false};
}

int64_t DateToMicroseconds(date_t date) {
    return static_cast<int64_t>(date.days) * 24LL * 60LL * 60LL * 1000000LL;
}

int64_t TimestampToMicroseconds(timestamp_t ts) {
    return ts.value;
}

date_t MicrosecondsToDate(int64_t micros) {
    int32_t days = static_cast<int32_t>(micros / (24LL * 60LL * 60LL * 1000000LL));
    return date_t(days);
}

timestamp_t MicrosecondsToTimestamp(int64_t micros) {
    return timestamp_t(micros);
}

string GetGroupKey(const Value &group_value) {
    if (group_value.IsNull()) {
        return "__NULL__";
    }
    return group_value.ToString();
}

// ============================================================================
// Bind Data
// ============================================================================

struct TsFillGapsNativeBindData : public TableFunctionData {
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;  // True if parsed as pure integer (for integer date columns)
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType::TIMESTAMP;
    LogicalType group_logical_type = LogicalType::VARCHAR;
};

// ============================================================================
// Local State - buffers data per thread
// ============================================================================

struct TsFillGapsNativeLocalState : public LocalTableFunctionState {
    // Buffer for incoming data per group
    struct GroupData {
        Value group_value;
        vector<int64_t> dates;
        vector<double> values;
        vector<bool> validity;
    };

    // Map group key -> accumulated data
    std::map<string, GroupData> groups;
    vector<string> group_order;  // Track insertion order

    // Filled results ready to output
    struct FilledGroup {
        Value group_value;
        vector<int64_t> dates;
        vector<double> values;
        vector<bool> validity;
    };
    vector<FilledGroup> filled_results;
    idx_t current_group = 0;
    idx_t current_row = 0;
    bool processed = false;
};

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsFillGapsNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsFillGapsNativeBindData>();

    // Parse frequency from second argument (index 1, since index 0 is TABLE placeholder)
    if (input.inputs.size() >= 2) {
        string freq_str = input.inputs[1].GetValue<string>();
        auto [freq, is_raw] = ParseFrequencyToSeconds(freq_str);
        bind_data->frequency_seconds = freq;
        bind_data->frequency_is_raw = is_raw;
    }

    // Input table must have exactly 3 columns: group, date, value
    if (input.input_table_types.size() != 3) {
        throw InvalidInputException(
            "ts_fill_gaps_native requires input with exactly 3 columns: group_col, date_col, value_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Detect date column type from input (column 1)
    bind_data->group_logical_type = input.input_table_types[0];
    bind_data->date_logical_type = input.input_table_types[1];

    switch (input.input_table_types[1].id()) {
        case LogicalTypeId::DATE:
            bind_data->date_col_type = DateColumnType::DATE;
            break;
        case LogicalTypeId::TIMESTAMP:
        case LogicalTypeId::TIMESTAMP_TZ:
            bind_data->date_col_type = DateColumnType::TIMESTAMP;
            break;
        case LogicalTypeId::INTEGER:
            bind_data->date_col_type = DateColumnType::INTEGER;
            break;
        case LogicalTypeId::BIGINT:
            bind_data->date_col_type = DateColumnType::BIGINT;
            break;
        default:
            throw InvalidInputException(
                "Date column must be DATE, TIMESTAMP, INTEGER, or BIGINT, got: %s",
                input.input_table_types[1].ToString().c_str());
    }

    // Output schema: group_col, date_col, value_col with preserved types
    names.push_back(input.input_table_names[0]);
    return_types.push_back(bind_data->group_logical_type);

    names.push_back(input.input_table_names[1]);
    return_types.push_back(bind_data->date_logical_type);

    names.push_back(input.input_table_names[2]);
    return_types.push_back(LogicalType::DOUBLE);

    return bind_data;
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsFillGapsNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<GlobalTableFunctionState>();
}

static unique_ptr<LocalTableFunctionState> TsFillGapsNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsFillGapsNativeLocalState>();
}

// ============================================================================
// In-Out Function - receives streaming input
// ============================================================================

static OperatorResultType TsFillGapsNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsFillGapsNativeBindData>();
    auto &local_state = data_p.local_state->Cast<TsFillGapsNativeLocalState>();

    // Buffer all incoming data - we need complete groups before processing
    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull()) continue;

        string group_key = GetGroupKey(group_val);

        if (local_state.groups.find(group_key) == local_state.groups.end()) {
            local_state.groups[group_key] = TsFillGapsNativeLocalState::GroupData();
            local_state.groups[group_key].group_value = group_val;
            local_state.group_order.push_back(group_key);
        }

        auto &grp = local_state.groups[group_key];

        // Convert date to microseconds for Rust
        int64_t date_micros;
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                date_micros = DateToMicroseconds(date_val.GetValue<date_t>());
                break;
            case DateColumnType::TIMESTAMP:
                date_micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
                break;
            case DateColumnType::INTEGER:
                date_micros = date_val.GetValue<int32_t>();
                break;
            case DateColumnType::BIGINT:
                date_micros = date_val.GetValue<int64_t>();
                break;
        }

        grp.dates.push_back(date_micros);
        grp.values.push_back(value_val.IsNull() ? 0.0 : value_val.GetValue<double>());
        grp.validity.push_back(!value_val.IsNull());
    }

    // Don't output anything during input phase - wait for finalize
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - process accumulated data and output results
// ============================================================================

static OperatorFinalizeResultType TsFillGapsNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsFillGapsNativeBindData>();
    auto &local_state = data_p.local_state->Cast<TsFillGapsNativeLocalState>();

    // Process all groups on first finalize call
    if (!local_state.processed) {
        for (const auto &group_key : local_state.group_order) {
            auto &grp = local_state.groups[group_key];

            if (grp.dates.empty()) continue;

            // Build validity bitmask for Rust
            size_t validity_words = (grp.dates.size() + 63) / 64;
            vector<uint64_t> validity(validity_words, 0);
            for (size_t i = 0; i < grp.validity.size(); i++) {
                if (grp.validity[i]) {
                    validity[i / 64] |= (1ULL << (i % 64));
                }
            }

            // Convert frequency for Rust based on date type
            int64_t freq_for_rust;
            if (bind_data.date_col_type == DateColumnType::INTEGER ||
                bind_data.date_col_type == DateColumnType::BIGINT) {
                // For integer date columns with raw integer frequency, use as-is
                // For integer date columns with time-based frequency, use seconds
                if (bind_data.frequency_is_raw) {
                    freq_for_rust = bind_data.frequency_seconds;  // Raw value
                } else {
                    freq_for_rust = bind_data.frequency_seconds;  // Time unit in seconds
                }
            } else {
                // For DATE/TIMESTAMP, convert to microseconds
                // If raw integer, interpret as days first
                if (bind_data.frequency_is_raw) {
                    freq_for_rust = bind_data.frequency_seconds * 86400LL * 1000000LL;  // days -> microseconds
                } else {
                    freq_for_rust = bind_data.frequency_seconds * 1000000LL;  // seconds -> microseconds
                }
            }

            // Call Rust FFI
            GapFillResult ffi_result = {};
            AnofoxError error = {};

            bool success = anofox_ts_fill_gaps(
                grp.dates.data(),
                grp.values.data(),
                validity.empty() ? nullptr : validity.data(),
                grp.dates.size(),
                freq_for_rust,
                &ffi_result,
                &error
            );

            if (!success) {
                throw InvalidInputException("ts_fill_gaps failed: %s",
                    error.message ? error.message : "Unknown error");
            }

            // Store results
            TsFillGapsNativeLocalState::FilledGroup filled;
            filled.group_value = grp.group_value;

            for (size_t i = 0; i < ffi_result.length; i++) {
                filled.dates.push_back(ffi_result.dates[i]);
                filled.values.push_back(ffi_result.values[i]);

                bool valid = false;
                if (ffi_result.validity) {
                    valid = (ffi_result.validity[i / 64] >> (i % 64)) & 1;
                }
                filled.validity.push_back(valid);
            }

            local_state.filled_results.push_back(std::move(filled));

            anofox_free_gap_fill_result(&ffi_result);
        }
        local_state.processed = true;
    }

    // Output results
    if (local_state.filled_results.empty() ||
        local_state.current_group >= local_state.filled_results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t output_count = 0;

    while (output_count < STANDARD_VECTOR_SIZE &&
           local_state.current_group < local_state.filled_results.size()) {

        auto &grp = local_state.filled_results[local_state.current_group];

        while (output_count < STANDARD_VECTOR_SIZE &&
               local_state.current_row < grp.dates.size()) {

            idx_t out_idx = output_count;

            // Group column
            output.data[0].SetValue(out_idx, grp.group_value);

            // Date column (with type preservation!)
            int64_t date_micros = grp.dates[local_state.current_row];
            switch (bind_data.date_col_type) {
                case DateColumnType::DATE:
                    output.data[1].SetValue(out_idx, Value::DATE(MicrosecondsToDate(date_micros)));
                    break;
                case DateColumnType::TIMESTAMP:
                    output.data[1].SetValue(out_idx, Value::TIMESTAMP(MicrosecondsToTimestamp(date_micros)));
                    break;
                case DateColumnType::INTEGER:
                    output.data[1].SetValue(out_idx, Value::INTEGER(static_cast<int32_t>(date_micros)));
                    break;
                case DateColumnType::BIGINT:
                    output.data[1].SetValue(out_idx, Value::BIGINT(date_micros));
                    break;
            }

            // Value column
            if (grp.validity[local_state.current_row]) {
                output.data[2].SetValue(out_idx, Value::DOUBLE(grp.values[local_state.current_row]));
            } else {
                output.data[2].SetValue(out_idx, Value());
            }

            output_count++;
            local_state.current_row++;
        }

        if (local_state.current_row >= grp.dates.size()) {
            local_state.current_group++;
            local_state.current_row = 0;
        }
    }

    output.SetCardinality(output_count);

    if (local_state.current_group >= local_state.filled_results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsFillGapsNativeFunction(ExtensionLoader &loader) {
    // Table-in-out function: (TABLE, frequency)
    // Input table must have 3 columns: group_col, date_col, value_col
    TableFunction func("ts_fill_gaps_native",
        {LogicalType::TABLE, LogicalType::VARCHAR},
        nullptr,  // No execute function - use in_out_function
        TsFillGapsNativeBind,
        TsFillGapsNativeInitGlobal,
        TsFillGapsNativeInitLocal);

    func.in_out_function = TsFillGapsNativeInOut;
    func.in_out_function_final = TsFillGapsNativeFinalize;

    loader.RegisterFunction(func);

    // Also register with anofox_fcst prefix
    TableFunction anofox_func = func;
    anofox_func.name = "anofox_fcst_ts_fill_gaps_native";
    loader.RegisterFunction(anofox_func);
}

} // namespace duckdb
