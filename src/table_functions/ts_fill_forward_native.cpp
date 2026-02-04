#include "ts_fill_gaps_native.hpp"  // Reuse helper functions
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>

namespace duckdb {

// ============================================================================
// Bind Data
// ============================================================================

struct TsFillForwardNativeBindData : public TableFunctionData {
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;
    FrequencyType frequency_type = FrequencyType::FIXED;  // Calendar vs fixed frequency
    int64_t target_date_micros = 0;
    bool target_is_raw = false;
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
};

// ============================================================================
// Shared Group Data Structure
// ============================================================================

struct FillForwardGroupData {
    Value group_value;
    vector<int64_t> dates;
    vector<double> values;
    vector<bool> validity;
};

struct FillForwardFilledGroup {
    Value group_value;
    vector<int64_t> dates;
    vector<double> values;
    vector<bool> validity;
};

// ============================================================================
// Local State - minimal per-thread state
// ============================================================================

struct TsFillForwardNativeLocalState : public LocalTableFunctionState {
    // No longer needed - output iteration is in global state
};

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsFillForwardNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsFillForwardNativeBindData>();

    // Input table must have exactly 3 columns: group, date, value
    if (input.input_table_types.size() != 3) {
        throw InvalidInputException(
            "ts_fill_forward_native requires input with exactly 3 columns: group_col, date_col, value_col. Got %zu columns.",
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

    // Parse target_date (index 1, since index 0 is TABLE placeholder)
    if (input.inputs.size() >= 2 && !input.inputs[1].IsNull()) {
        auto &target_val = input.inputs[1];
        if (target_val.type().id() == LogicalTypeId::VARCHAR) {
            // For VARCHAR, try to parse as integer first
            string target_str = target_val.GetValue<string>();
            try {
                bind_data->target_date_micros = std::stoll(target_str);
                bind_data->target_is_raw = true;
            } catch (...) {
                // If not an integer, cast to timestamp
                Value casted = target_val.DefaultCastAs(LogicalType(LogicalTypeId::TIMESTAMP));
                bind_data->target_date_micros = casted.GetValue<timestamp_t>().value;
                bind_data->target_is_raw = false;
            }
        } else if (target_val.type().id() == LogicalTypeId::TIMESTAMP) {
            bind_data->target_date_micros = target_val.GetValue<timestamp_t>().value;
            bind_data->target_is_raw = false;
        } else if (target_val.type().id() == LogicalTypeId::DATE) {
            bind_data->target_date_micros = DateToMicroseconds(target_val.GetValue<date_t>());
            bind_data->target_is_raw = false;
        } else if (target_val.type().id() == LogicalTypeId::INTEGER) {
            bind_data->target_date_micros = target_val.GetValue<int32_t>();
            bind_data->target_is_raw = true;
        } else if (target_val.type().id() == LogicalTypeId::BIGINT) {
            bind_data->target_date_micros = target_val.GetValue<int64_t>();
            bind_data->target_is_raw = true;
        }
    }

    // Parse frequency (index 2)
    if (input.inputs.size() >= 3 && !input.inputs[2].IsNull()) {
        string freq_str = input.inputs[2].GetValue<string>();
        auto parsed = ParseFrequencyWithType(freq_str);
        bind_data->frequency_seconds = parsed.seconds;
        bind_data->frequency_is_raw = parsed.is_raw;
        bind_data->frequency_type = parsed.type;
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
// Global State - thread-safe accumulation
//
// IMPORTANT: DuckDB's PhysicalTableInOutFunction::ParallelOperator() always
// returns true, ignoring MaxThreads(). To handle parallel execution correctly,
// we accumulate all input data in GlobalState (protected by mutex), then
// process everything in a single-threaded finalize phase.
// ============================================================================

struct TsFillForwardNativeGlobalState : public GlobalTableFunctionState {
    // Mutex to protect shared data structures
    mutex mtx;

    // Shared accumulator for all input data (protected by mtx)
    std::map<string, FillForwardGroupData> groups;
    vector<string> group_order;

    // Processed results (populated in finalize, read during output)
    vector<FillForwardFilledGroup> filled_results;
    bool processed = false;

    // Output iteration state (protected by mtx)
    idx_t current_group = 0;
    idx_t current_row = 0;

    idx_t MaxThreads() const override {
        return 1;  // Hint (ignored by DuckDB for in_out functions, but kept for documentation)
    }
};

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsFillForwardNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<TsFillForwardNativeGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsFillForwardNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsFillForwardNativeLocalState>();
}

// ============================================================================
// In-Out Function - receives streaming input
// ============================================================================

static OperatorResultType TsFillForwardNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsFillForwardNativeBindData>();
    auto &global_state = data_p.global_state->Cast<TsFillForwardNativeGlobalState>();

    // Lock the global state for thread-safe accumulation
    lock_guard<mutex> lock(global_state.mtx);

    // Buffer all incoming data in global state
    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull()) continue;

        string group_key = GetGroupKey(group_val);

        if (global_state.groups.find(group_key) == global_state.groups.end()) {
            global_state.groups[group_key] = FillForwardGroupData();
            global_state.groups[group_key].group_value = group_val;
            global_state.group_order.push_back(group_key);
        }

        auto &grp = global_state.groups[group_key];

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

        // Check for duplicate date within this group
        if (std::find(grp.dates.begin(), grp.dates.end(), date_micros) != grp.dates.end()) {
            throw InvalidInputException(
                "ts_fill_forward_by: Duplicate (group, date) pair detected. "
                "Group '%s' has multiple rows for the same date. "
                "Please deduplicate your input data before calling this function.",
                group_key.c_str());
        }

        grp.dates.push_back(date_micros);
        grp.values.push_back(value_val.IsNull() ? 0.0 : value_val.GetValue<double>());
        grp.validity.push_back(!value_val.IsNull());
    }

    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - process accumulated data and output results
// ============================================================================

static OperatorFinalizeResultType TsFillForwardNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsFillForwardNativeBindData>();
    auto &global_state = data_p.global_state->Cast<TsFillForwardNativeGlobalState>();

    // Lock for thread-safe access to global state
    lock_guard<mutex> lock(global_state.mtx);

    // Process all groups on first finalize call (only one thread does this)
    if (!global_state.processed) {
        for (const auto &group_key : global_state.group_order) {
            auto &grp = global_state.groups[group_key];

            if (grp.dates.empty()) continue;

            // Build validity bitmask for Rust
            size_t validity_words = (grp.dates.size() + 63) / 64;
            vector<uint64_t> validity(validity_words, 0);
            for (size_t i = 0; i < grp.validity.size(); i++) {
                if (grp.validity[i]) {
                    validity[i / 64] |= (1ULL << (i % 64));
                }
            }

            // Convert frequency and target_date for Rust based on date type
            int64_t freq_for_rust;
            int64_t target_for_rust;

            if (bind_data.date_col_type == DateColumnType::INTEGER ||
                bind_data.date_col_type == DateColumnType::BIGINT) {
                if (bind_data.frequency_is_raw) {
                    freq_for_rust = bind_data.frequency_seconds;
                } else {
                    freq_for_rust = bind_data.frequency_seconds;
                }
                target_for_rust = bind_data.target_date_micros;
            } else {
                if (bind_data.frequency_is_raw) {
                    freq_for_rust = bind_data.frequency_seconds * 86400LL * 1000000LL;
                } else {
                    freq_for_rust = bind_data.frequency_seconds * 1000000LL;
                }
                if (bind_data.target_is_raw) {
                    target_for_rust = bind_data.target_date_micros * 86400LL * 1000000LL;
                } else {
                    target_for_rust = bind_data.target_date_micros;
                }
            }

            // Call Rust FFI
            GapFillResult ffi_result = {};
            AnofoxError error = {};

            bool success = anofox_ts_fill_forward_dates(
                grp.dates.data(),
                grp.values.data(),
                validity.empty() ? nullptr : validity.data(),
                grp.dates.size(),
                target_for_rust,
                freq_for_rust,
                bind_data.frequency_type,
                &ffi_result,
                &error
            );

            if (!success) {
                throw InvalidInputException("ts_fill_forward failed: %s",
                    error.message ? error.message : "Unknown error");
            }

            // Store results in global state
            FillForwardFilledGroup filled;
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

            global_state.filled_results.push_back(std::move(filled));

            anofox_free_gap_fill_result(&ffi_result);
        }
        global_state.processed = true;
    }

    // Output results (using global_state for iteration position)
    if (global_state.filled_results.empty() ||
        global_state.current_group >= global_state.filled_results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t output_count = 0;

    // Initialize all output vectors as FLAT_VECTOR
    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    while (output_count < STANDARD_VECTOR_SIZE &&
           global_state.current_group < global_state.filled_results.size()) {

        auto &grp = global_state.filled_results[global_state.current_group];

        while (output_count < STANDARD_VECTOR_SIZE &&
               global_state.current_row < grp.dates.size()) {

            idx_t out_idx = output_count;

            // Group column
            output.data[0].SetValue(out_idx, grp.group_value);

            // Date column (with type preservation!)
            int64_t date_micros = grp.dates[global_state.current_row];
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
            if (grp.validity[global_state.current_row]) {
                output.data[2].SetValue(out_idx, Value::DOUBLE(grp.values[global_state.current_row]));
            } else {
                output.data[2].SetValue(out_idx, Value());
            }

            output_count++;
            global_state.current_row++;
        }

        if (global_state.current_row >= grp.dates.size()) {
            global_state.current_group++;
            global_state.current_row = 0;
        }
    }

    output.SetCardinality(output_count);

    if (global_state.current_group >= global_state.filled_results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }

    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsFillForwardNativeFunction(ExtensionLoader &loader) {
    // Internal table-in-out function: (TABLE, target_date, frequency)
    // Input table must have 3 columns: group_col, date_col, value_col
    // Note: This is an internal function (prefixed with _) called by ts_fill_forward_by macro
    TableFunction func("_ts_fill_forward_native",
        {LogicalType::TABLE, LogicalType::ANY, LogicalType(LogicalTypeId::VARCHAR)},
        nullptr,
        TsFillForwardNativeBind,
        TsFillForwardNativeInitGlobal,
        TsFillForwardNativeInitLocal);

    func.in_out_function = TsFillForwardNativeInOut;
    func.in_out_function_final = TsFillForwardNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
