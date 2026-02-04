#include "ts_fill_gaps_native.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/hash.hpp"
#include "duckdb/parallel/task_scheduler.hpp"
#include <algorithm>
#include <regex>
#include <cmath>
#include <map>
#include <atomic>

namespace duckdb {

// ============================================================================
// Helper Functions
// ============================================================================

// Returns frequency info including type for calendar frequencies
ParsedFrequency ParseFrequencyWithType(const string &frequency_str) {
    string upper = StringUtil::Upper(frequency_str);
    StringUtil::Trim(upper);

    // Try to parse Polars-style frequency (e.g., "1d", "1h", "30m")
    std::regex polars_regex("^([0-9]+)(d|h|m|min|w|mo|q|y)$", std::regex::icase);
    std::smatch match;

    if (std::regex_match(upper, match, polars_regex)) {
        int64_t count = std::stoll(match[1].str());
        string unit = StringUtil::Lower(match[2].str());

        if (unit == "d") return {count * 86400, false, FrequencyType::FIXED};
        if (unit == "h") return {count * 3600, false, FrequencyType::FIXED};
        if (unit == "m" || unit == "min") return {count * 60, false, FrequencyType::FIXED};
        if (unit == "w") return {count * 86400 * 7, false, FrequencyType::FIXED};
        if (unit == "mo") return {count, false, FrequencyType::MONTHLY};
        if (unit == "q") return {count, false, FrequencyType::QUARTERLY};
        if (unit == "y") return {count, false, FrequencyType::YEARLY};
    }

    // Try to parse DuckDB INTERVAL style (e.g., "1 day", "1 hour")
    std::regex interval_regex("^([0-9]+)\\s*(day|days|hour|hours|minute|minutes|week|weeks|month|months|quarter|quarters|year|years)$", std::regex::icase);

    if (std::regex_match(upper, match, interval_regex)) {
        int64_t count = std::stoll(match[1].str());
        string unit = StringUtil::Lower(match[2].str());

        if (unit == "day" || unit == "days") return {count * 86400, false, FrequencyType::FIXED};
        if (unit == "hour" || unit == "hours") return {count * 3600, false, FrequencyType::FIXED};
        if (unit == "minute" || unit == "minutes") return {count * 60, false, FrequencyType::FIXED};
        if (unit == "week" || unit == "weeks") return {count * 86400 * 7, false, FrequencyType::FIXED};
        if (unit == "month" || unit == "months") return {count, false, FrequencyType::MONTHLY};
        if (unit == "quarter" || unit == "quarters") return {count, false, FrequencyType::QUARTERLY};
        if (unit == "year" || unit == "years") return {count, false, FrequencyType::YEARLY};
    }

    // Try to parse as pure integer - mark as raw for integer date columns
    std::regex int_regex("^[0-9]+$");
    if (std::regex_match(upper, int_regex)) {
        // For pure integers, store the raw value. The caller will interpret based on date type.
        return {std::stoll(upper), true, FrequencyType::FIXED};
    }

    // Invalid frequency - throw error with helpful message
    throw InvalidInputException(
        "Invalid frequency '%s'. Valid formats:\n"
        "  Polars-style: '1d', '1h', '30m', '1w', '1mo', '1q', '1y'\n"
        "  DuckDB INTERVAL: '1 day', '1 hour', '1 minute', '1 week', '1 month', '1 quarter', '1 year'\n"
        "  Raw integer: '86400' (for integer date columns)",
        frequency_str.c_str());
}

// Legacy function for backward compatibility
std::pair<int64_t, bool> ParseFrequencyToSeconds(const string &frequency_str) {
    auto parsed = ParseFrequencyWithType(frequency_str);
    return {parsed.seconds, parsed.is_raw};
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
    FrequencyType frequency_type = FrequencyType::FIXED;  // Calendar vs fixed frequency
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
};

// ============================================================================
// Shared Group Data Structure
// ============================================================================

struct FillGapsGroupData {
    Value group_value;
    vector<int64_t> dates;
    vector<double> values;
    vector<bool> validity;
};

struct FillGapsFilledGroup {
    Value group_value;
    vector<int64_t> dates;
    vector<double> values;
    vector<bool> validity;
};

// ============================================================================
// Per-Slot Storage - hash-based partitioning for parallel execution
// ============================================================================

struct FillGapsSlot {
    mutex mtx;  // Per-slot mutex for thread-safe writes
    std::map<string, FillGapsGroupData> groups;
    vector<string> group_order;

    // Processing and output state
    vector<FillGapsFilledGroup> results;
    bool processed = false;
    idx_t current_group = 0;
    idx_t current_row = 0;
};

// ============================================================================
// Local State - tracks which slot this thread is outputting from
// ============================================================================

struct TsFillGapsNativeLocalState : public LocalTableFunctionState {
    idx_t current_slot = 0;  // Which slot we're currently outputting from
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
        auto parsed = ParseFrequencyWithType(freq_str);
        bind_data->frequency_seconds = parsed.seconds;
        bind_data->frequency_is_raw = parsed.is_raw;
        bind_data->frequency_type = parsed.type;
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
    return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));

    return bind_data;
}

// ============================================================================
// Global State - hash-based slot partitioning for parallel execution
//
// Groups are assigned to slots based on hash(group_key) % num_slots.
// Each slot has its own mutex, allowing parallel writes to different slots.
// This provides good parallelism when groups are distributed across slots.
// ============================================================================

struct TsFillGapsNativeGlobalState : public GlobalTableFunctionState {
    idx_t num_slots;
    vector<unique_ptr<FillGapsSlot>> slots;

    // Atomic counter for finalize slot assignment
    std::atomic<idx_t> next_output_slot{0};

    idx_t MaxThreads() const override {
        return num_slots;
    }
};

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsFillGapsNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    auto state = make_uniq<TsFillGapsNativeGlobalState>();
    state->num_slots = TaskScheduler::GetScheduler(context).NumberOfThreads();
    if (state->num_slots == 0) state->num_slots = 1;

    // Pre-allocate slots
    state->slots.resize(state->num_slots);
    for (idx_t i = 0; i < state->num_slots; i++) {
        state->slots[i] = make_uniq<FillGapsSlot>();
    }

    return state;
}

static unique_ptr<LocalTableFunctionState> TsFillGapsNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsFillGapsNativeLocalState>();
}

// ============================================================================
// In-Out Function - batched slot assignment to minimize lock acquisitions
// ============================================================================

// Temporary row data for batching
struct TempRow {
    Value group_val;
    int64_t date_micros;
    double value;
    bool valid;
};

static OperatorResultType TsFillGapsNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsFillGapsNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsFillGapsNativeGlobalState>();

    // Step 1: Collect all rows locally, grouped by slot (no locking)
    vector<vector<std::pair<string, TempRow>>> slot_batches(gstate.num_slots);

    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull()) continue;

        string group_key = GetGroupKey(group_val);

        // Convert date to microseconds
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

        // Hash-based slot assignment
        hash_t group_hash = Hash(group_key.c_str(), group_key.size());
        idx_t slot_idx = group_hash % gstate.num_slots;

        TempRow row;
        row.group_val = group_val;
        row.date_micros = date_micros;
        row.value = value_val.IsNull() ? 0.0 : value_val.GetValue<double>();
        row.valid = !value_val.IsNull();

        slot_batches[slot_idx].emplace_back(std::move(group_key), std::move(row));
    }

    // Step 2: Lock each slot once and insert all its rows
    for (idx_t slot_idx = 0; slot_idx < gstate.num_slots; slot_idx++) {
        auto &batch = slot_batches[slot_idx];
        if (batch.empty()) continue;

        auto &slot = *gstate.slots[slot_idx];
        lock_guard<mutex> lock(slot.mtx);

        for (auto &[group_key, row] : batch) {
            if (slot.groups.find(group_key) == slot.groups.end()) {
                slot.groups[group_key] = FillGapsGroupData();
                slot.groups[group_key].group_value = row.group_val;
                slot.group_order.push_back(group_key);
            }

            auto &grp = slot.groups[group_key];

            // Check for duplicate date within this group
            if (std::find(grp.dates.begin(), grp.dates.end(), row.date_micros) != grp.dates.end()) {
                throw InvalidInputException(
                    "ts_fill_gaps_by: Duplicate (group, date) pair detected. "
                    "Group '%s' has multiple rows for the same date. "
                    "Please deduplicate your input data before calling this function.",
                    group_key.c_str());
            }

            grp.dates.push_back(row.date_micros);
            grp.values.push_back(row.value);
            grp.validity.push_back(row.valid);
        }
    }

    // Don't output anything during input phase - wait for finalize
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - parallel processing and output by slot
//
// Each thread claims and processes slots. Multiple threads can process
// different slots in parallel.
// ============================================================================

static OperatorFinalizeResultType TsFillGapsNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsFillGapsNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsFillGapsNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsFillGapsNativeLocalState>();

    // Find the next slot to output from
    while (lstate.current_slot < gstate.num_slots) {
        auto &slot = *gstate.slots[lstate.current_slot];

        // Lock the slot to check/update state
        lock_guard<mutex> lock(slot.mtx);

        // Process this slot if not yet processed
        if (!slot.processed) {
            for (const auto &group_key : slot.group_order) {
                auto &grp = slot.groups[group_key];

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
                    freq_for_rust = bind_data.frequency_seconds;
                } else {
                    if (bind_data.frequency_is_raw) {
                        freq_for_rust = bind_data.frequency_seconds * 86400LL * 1000000LL;
                    } else {
                        freq_for_rust = bind_data.frequency_seconds * 1000000LL;
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
                    bind_data.frequency_type,
                    &ffi_result,
                    &error
                );

                if (!success) {
                    throw InvalidInputException("ts_fill_gaps failed: %s",
                        error.message ? error.message : "Unknown error");
                }

                // Store results
                FillGapsFilledGroup filled;
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

                slot.results.push_back(std::move(filled));

                anofox_free_gap_fill_result(&ffi_result);
            }
            slot.processed = true;
        }

        // Check if this slot has more output
        if (slot.results.empty() || slot.current_group >= slot.results.size()) {
            // This slot is done, move to next slot
            lstate.current_slot++;
            continue;
        }

        // Output from this slot
        idx_t output_count = 0;

        // Initialize all output vectors as FLAT_VECTOR
        for (idx_t col = 0; col < output.ColumnCount(); col++) {
            output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
        }

        while (output_count < STANDARD_VECTOR_SIZE && slot.current_group < slot.results.size()) {
            auto &grp = slot.results[slot.current_group];

            while (output_count < STANDARD_VECTOR_SIZE && slot.current_row < grp.dates.size()) {
                idx_t out_idx = output_count;

                // Group column
                output.data[0].SetValue(out_idx, grp.group_value);

                // Date column (with type preservation!)
                int64_t date_micros = grp.dates[slot.current_row];
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
                if (grp.validity[slot.current_row]) {
                    output.data[2].SetValue(out_idx, Value::DOUBLE(grp.values[slot.current_row]));
                } else {
                    output.data[2].SetValue(out_idx, Value());
                }

                output_count++;
                slot.current_row++;
            }

            if (slot.current_row >= grp.dates.size()) {
                slot.current_group++;
                slot.current_row = 0;
            }
        }

        output.SetCardinality(output_count);

        // Check if we need to continue with this slot or move to next
        if (slot.current_group >= slot.results.size()) {
            lstate.current_slot++;
        }

        return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
    }

    // All slots processed
    return OperatorFinalizeResultType::FINISHED;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsFillGapsNativeFunction(ExtensionLoader &loader) {
    // Internal table-in-out function: (TABLE, frequency)
    // Input table must have 3 columns: group_col, date_col, value_col
    // Note: This is an internal function (prefixed with _) called by ts_fill_gaps_by macro
    TableFunction func("_ts_fill_gaps_native",
        {LogicalType::TABLE, LogicalType(LogicalTypeId::VARCHAR)},
        nullptr,  // No execute function - use in_out_function
        TsFillGapsNativeBind,
        TsFillGapsNativeInitGlobal,
        TsFillGapsNativeInitLocal);

    func.in_out_function = TsFillGapsNativeInOut;
    func.in_out_function_final = TsFillGapsNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
