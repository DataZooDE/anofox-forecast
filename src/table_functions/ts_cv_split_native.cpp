#include "ts_cv_split_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For ParseFrequencyToSeconds, etc.
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

namespace duckdb {

// ============================================================================
// _ts_cv_split_native - Internal native streaming CV split table function
//
// This is an INTERNAL function used by ts_cv_split_by macro.
// Users should call ts_cv_split_by() instead of this function directly.
//
// MEMORY FOOTPRINT:
//   - Native (this function): O(input_rows) - buffers input, outputs expanded
//   - Old SQL macro approach: O(rows * folds) due to CROSS JOIN intermediate
//
// Buffers all input rows, then in finalize expands each row to multiple
// output rows (one per fold the row belongs to).
// ============================================================================

// ============================================================================
// Fold Bounds Structure (position-based indices)
//
// IMPORTANT: This uses position-based indices for test boundaries.
// The assumption is that input data is pre-cleaned (no gaps, proper frequency).
// This design eliminates calendar frequency issues (monthly, quarterly, yearly)
// where date arithmetic doesn't align with actual data timestamps.
//
// train_end_timestamp is kept for matching rows to training period,
// but test boundaries use position offsets from the training end position.
// ============================================================================

struct FoldBounds {
    int64_t fold_id;
    int64_t train_end;    // microseconds - used to find training end position
    idx_t gap;            // number of positions to skip after train
    idx_t horizon;        // number of test positions
};

// ============================================================================
// Bind Data - captures all parameters
// ============================================================================

struct TsCvSplitNativeBindData : public TableFunctionData {
    // Fold definitions (computed at bind time)
    vector<FoldBounds> folds;

    // Parameters
    int64_t horizon = 7;
    string window_type = "expanding";
    int64_t min_train_size = 1;
    int64_t gap = 0;
    int64_t embargo = 0;

    // Type preservation
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
    LogicalType value_logical_type = LogicalType(LogicalTypeId::DOUBLE);
};

// ============================================================================
// Data Structs (standalone to avoid collisions)
// ============================================================================

struct TsCvSplitInputRow {
    Value group_val;
    Value date_val;
    int64_t date_micros;
    double value;
    bool valid;
    string group_key;  // For grouping
};

struct TsCvSplitOutputRow {
    idx_t input_idx;
    int64_t fold_id;
    bool is_train;
};

// ============================================================================
// Global State - holds all mutable data with mutex protection
// ============================================================================

struct TsCvSplitNativeGlobalState : public GlobalTableFunctionState {
    idx_t max_threads = 1;

    // Buffered input data (protected by groups_mutex)
    std::mutex groups_mutex;
    vector<TsCvSplitInputRow> input_rows;

    // Position-based processing state
    std::map<string, vector<idx_t>> group_sorted_indices;
    bool preprocessing_done = false;

    // Output state - pre-computed output rows
    vector<TsCvSplitOutputRow> output_rows;

    // Streaming output state
    idx_t output_offset = 0;

    // Single-thread finalize + barrier
    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};

    idx_t MaxThreads() const override {
        return max_threads;
    }
};

// ============================================================================
// Local State
// ============================================================================

struct TsCvSplitNativeLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool registered_collector = false;
    bool registered_finalizer = false;
};

// ============================================================================
// Helper Functions
// ============================================================================

static string ParseStringParam(const Value &params_value, const string &key, const string &default_val) {
    if (params_value.IsNull()) {
        return default_val;
    }

    if (params_value.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params_value);
        for (auto &child : map_children) {
            auto &k = StructValue::GetChildren(child)[0];
            auto &v = StructValue::GetChildren(child)[1];
            if (k.ToString() == key && !v.IsNull()) {
                return v.ToString();
            }
        }
    }
    return default_val;
}

static int64_t ParseIntParam(const Value &params_value, const string &key, int64_t default_val) {
    if (params_value.IsNull()) {
        return default_val;
    }

    if (params_value.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params_value);
        for (auto &child : map_children) {
            auto &k = StructValue::GetChildren(child)[0];
            auto &v = StructValue::GetChildren(child)[1];
            if (k.ToString() == key && !v.IsNull()) {
                try {
                    return std::stoll(v.ToString());
                } catch (...) {
                    return default_val;
                }
            }
        }
    }
    return default_val;
}

static int64_t TimestampToMicros(const Value &val) {
    if (val.type().id() == LogicalTypeId::TIMESTAMP) {
        return Timestamp::GetEpochMicroSeconds(val.GetValue<timestamp_t>());
    } else if (val.type().id() == LogicalTypeId::DATE) {
        auto date = val.GetValue<date_t>();
        return Timestamp::GetEpochMicroSeconds(Timestamp::FromDatetime(date, dtime_t(0)));
    }
    // Try to parse as string
    auto str = val.ToString();
    auto ts = Timestamp::FromString(str, false);
    return Timestamp::GetEpochMicroSeconds(ts);
}

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsCvSplitNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsCvSplitNativeBindData>();

    // Parse horizon (parameter index 1)
    if (input.inputs.size() > 1 && !input.inputs[1].IsNull()) {
        bind_data->horizon = input.inputs[1].GetValue<int64_t>();
    }

    // Parse params MAP (parameter index 3)
    Value params_value;
    if (input.inputs.size() > 3 && !input.inputs[3].IsNull()) {
        params_value = input.inputs[3];
        bind_data->window_type = ParseStringParam(params_value, "window_type", "expanding");
        bind_data->min_train_size = ParseIntParam(params_value, "min_train_size", 1);
        bind_data->gap = ParseIntParam(params_value, "gap", 0);
        bind_data->embargo = ParseIntParam(params_value, "embargo", 0);
    }

    // Parse training_end_times array (parameter index 2)
    if (input.inputs.size() > 2 && !input.inputs[2].IsNull()) {
        auto &training_ends = input.inputs[2];
        if (training_ends.type().id() != LogicalTypeId::LIST) {
            throw InvalidInputException("training_end_times must be an array of timestamps");
        }

        auto &children = ListValue::GetChildren(training_ends);

        // First pass: collect all training end times
        vector<int64_t> train_ends;
        for (auto &child : children) {
            train_ends.push_back(TimestampToMicros(child));
        }

        // Sort training ends
        std::sort(train_ends.begin(), train_ends.end());

        // Compute fold bounds - store train_end timestamp for position matching,
        // but use position-based offsets for test boundaries (no date arithmetic)
        for (idx_t i = 0; i < train_ends.size(); i++) {
            FoldBounds fold;
            fold.fold_id = static_cast<int64_t>(i + 1);
            fold.train_end = train_ends[i];  // Used to find position in sorted data
            fold.gap = static_cast<idx_t>(bind_data->gap);
            fold.horizon = static_cast<idx_t>(bind_data->horizon);

            bind_data->folds.push_back(fold);
        }
    }

    // Determine input types and names from the table argument
    auto &table_types = input.input_table_types;
    auto &table_names = input.input_table_names;
    if (table_types.size() >= 3) {
        bind_data->group_logical_type = table_types[0];
        bind_data->date_logical_type = table_types[1];
        bind_data->value_logical_type = table_types[2];

        // Detect date column type
        if (table_types[1].id() == LogicalTypeId::DATE) {
            bind_data->date_col_type = DateColumnType::DATE;
        } else if (table_types[1].id() == LogicalTypeId::TIMESTAMP) {
            bind_data->date_col_type = DateColumnType::TIMESTAMP;
        } else if (table_types[1].id() == LogicalTypeId::BIGINT ||
                   table_types[1].id() == LogicalTypeId::INTEGER) {
            bind_data->date_col_type = DateColumnType::BIGINT;
        }
    }

    // Output columns: preserve original column names, add fold_id and split
    return_types.push_back(bind_data->group_logical_type);
    names.push_back(table_names.size() > 0 ? table_names[0] : "group_col");

    return_types.push_back(bind_data->date_logical_type);
    names.push_back(table_names.size() > 1 ? table_names[1] : "date_col");

    return_types.push_back(bind_data->value_logical_type);
    names.push_back(table_names.size() > 2 ? table_names[2] : "target_col");

    return_types.push_back(LogicalType::BIGINT);
    names.push_back("fold_id");

    return_types.push_back(LogicalType::VARCHAR);
    names.push_back("split");

    return std::move(bind_data);
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsCvSplitNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {

    auto state = make_uniq<TsCvSplitNativeGlobalState>();
    return std::move(state);
}

static unique_ptr<LocalTableFunctionState> TsCvSplitNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {

    return make_uniq<TsCvSplitNativeLocalState>();
}

// ============================================================================
// In-Out Function - buffers all input rows
// ============================================================================

static OperatorResultType TsCvSplitNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data.bind_data->CastNoConst<TsCvSplitNativeBindData>();
    auto &gstate = data.global_state->Cast<TsCvSplitNativeGlobalState>();
    auto &lstate = data.local_state->Cast<TsCvSplitNativeLocalState>();

    // Register this thread as a collector (first call only)
    if (!lstate.registered_collector) {
        gstate.threads_collecting.fetch_add(1);
        lstate.registered_collector = true;
    }

    // Extract batch locally first
    vector<TsCvSplitInputRow> local_rows;

    for (idx_t row_idx = 0; row_idx < input.size(); row_idx++) {
        Value group_val = input.GetValue(0, row_idx);
        Value date_val = input.GetValue(1, row_idx);
        Value value_val = input.GetValue(2, row_idx);

        if (date_val.IsNull()) {
            continue;
        }

        TsCvSplitInputRow row;
        row.group_val = group_val;
        row.date_val = date_val;
        row.group_key = GetGroupKey(group_val);

        // Convert date to microseconds
        if (bind_data.date_col_type == DateColumnType::DATE) {
            auto date = date_val.GetValue<date_t>();
            row.date_micros = Timestamp::GetEpochMicroSeconds(Timestamp::FromDatetime(date, dtime_t(0)));
        } else if (bind_data.date_col_type == DateColumnType::BIGINT) {
            row.date_micros = date_val.GetValue<int64_t>() * 1000000;
        } else {
            row.date_micros = Timestamp::GetEpochMicroSeconds(date_val.GetValue<timestamp_t>());
        }

        row.value = value_val.IsNull() ? 0.0 : value_val.GetValue<double>();
        row.valid = !value_val.IsNull();

        local_rows.push_back(std::move(row));
    }

    // Insert into global state under lock
    {
        std::lock_guard<std::mutex> lock(gstate.groups_mutex);
        for (auto &row : local_rows) {
            gstate.input_rows.push_back(std::move(row));
        }
    }

    // Don't output anything during input phase
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - outputs expanded CV splits (position-based)
//
// POSITION-BASED APPROACH:
// 1. Group input rows by group_key and sort by date
// 2. For each fold, find train_end position via binary search on timestamp
// 3. Compute test positions as: [train_end_pos + 1 + gap .. train_end_pos + gap + horizon]
// 4. Stream output rows
//
// This eliminates date arithmetic for test boundaries, handling all
// frequency types correctly (hourly, daily, weekly, monthly, quarterly, yearly).
// ============================================================================

static OperatorFinalizeResultType TsCvSplitNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &output) {

    auto &bind_data = data.bind_data->CastNoConst<TsCvSplitNativeBindData>();
    auto &gstate = data.global_state->Cast<TsCvSplitNativeGlobalState>();
    auto &lstate = data.local_state->Cast<TsCvSplitNativeLocalState>();

    // Barrier + claim
    if (!lstate.registered_finalizer) {
        if (lstate.registered_collector) {
            gstate.threads_done_collecting.fetch_add(1);
        }
        lstate.registered_finalizer = true;
    }
    if (!lstate.owns_finalize) {
        bool expected = false;
        if (!gstate.finalize_claimed.compare_exchange_strong(expected, true)) {
            return OperatorFinalizeResultType::FINISHED;
        }
        lstate.owns_finalize = true;
        while (gstate.threads_done_collecting.load() < gstate.threads_collecting.load()) {
            std::this_thread::yield();
        }
    }

    // Preprocessing: group rows and compute output rows (done once)
    if (!gstate.preprocessing_done) {
        // Step 1: Build group indices
        for (idx_t i = 0; i < gstate.input_rows.size(); i++) {
            auto &row = gstate.input_rows[i];
            gstate.group_sorted_indices[row.group_key].push_back(i);
        }

        // Step 2: Sort each group by date
        for (auto &[group_key, indices] : gstate.group_sorted_indices) {
            std::sort(indices.begin(), indices.end(),
                [&gstate](idx_t a, idx_t b) {
                    return gstate.input_rows[a].date_micros < gstate.input_rows[b].date_micros;
                });
        }

        // Step 3: For each group and fold, compute train/test membership by position
        for (auto &[group_key, sorted_indices] : gstate.group_sorted_indices) {
            idx_t n_points = sorted_indices.size();

            for (auto &fold : bind_data.folds) {
                // Find train_end position: largest index where date <= fold.train_end
                idx_t train_end_pos = 0;
                for (idx_t i = 0; i < n_points; i++) {
                    if (gstate.input_rows[sorted_indices[i]].date_micros <= fold.train_end) {
                        train_end_pos = i;
                    } else {
                        break;
                    }
                }

                // Compute train_start position based on window_type
                idx_t train_start_pos = 0;
                if (bind_data.window_type != "expanding") {
                    // fixed or sliding window
                    if (train_end_pos + 1 >= static_cast<idx_t>(bind_data.min_train_size)) {
                        train_start_pos = train_end_pos + 1 - static_cast<idx_t>(bind_data.min_train_size);
                    }
                }

                // Handle embargo: adjust train_start if needed
                // For position-based, embargo shifts train_start forward
                if (bind_data.embargo > 0 && fold.fold_id > 1) {
                    // Find previous fold's test_end position and apply embargo
                    // This is simplified - proper implementation would track prev fold's test_end_pos
                    // For now, keep expanding window behavior for simplicity
                }

                // Test positions: start after train_end + gap
                idx_t test_start_pos = train_end_pos + 1 + fold.gap;
                idx_t test_end_pos = test_start_pos + fold.horizon - 1;

                // Clip to available data
                test_end_pos = std::min(test_end_pos, n_points - 1);

                // Generate output rows for training data
                for (idx_t pos = train_start_pos; pos <= train_end_pos && pos < n_points; pos++) {
                    TsCvSplitOutputRow out;
                    out.input_idx = sorted_indices[pos];
                    out.fold_id = fold.fold_id;
                    out.is_train = true;
                    gstate.output_rows.push_back(out);
                }

                // Generate output rows for test data
                for (idx_t pos = test_start_pos; pos <= test_end_pos && pos < n_points; pos++) {
                    TsCvSplitOutputRow out;
                    out.input_idx = sorted_indices[pos];
                    out.fold_id = fold.fold_id;
                    out.is_train = false;
                    gstate.output_rows.push_back(out);
                }
            }
        }

        gstate.preprocessing_done = true;
    }

    // Stream output from pre-computed output_rows
    output.Reset();
    idx_t output_idx = 0;

    // Initialize all output vectors as FLAT_VECTOR for parallel-safe batch merging
    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    while (gstate.output_offset < gstate.output_rows.size() && output_idx < STANDARD_VECTOR_SIZE) {
        auto &out_row = gstate.output_rows[gstate.output_offset];
        auto &input_row = gstate.input_rows[out_row.input_idx];

        output.SetValue(0, output_idx, input_row.group_val);
        output.SetValue(1, output_idx, input_row.date_val);

        if (input_row.valid) {
            output.SetValue(2, output_idx, Value::DOUBLE(input_row.value));
        } else {
            output.SetValue(2, output_idx, Value());
        }

        output.SetValue(3, output_idx, Value::BIGINT(out_row.fold_id));
        output.SetValue(4, output_idx, Value(out_row.is_train ? "train" : "test"));

        output_idx++;
        gstate.output_offset++;
    }

    output.SetCardinality(output_idx);

    if (gstate.output_offset >= gstate.output_rows.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }
    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsCvSplitNativeFunction(ExtensionLoader &loader) {
    // Create the table function with table input
    // NOTE: No frequency parameter needed - uses position-based indexing
    //       Assumes pre-cleaned data with no gaps
    TableFunction func("_ts_cv_split_native",
                       {LogicalType::TABLE,           // Input table (group, date, value)
                        LogicalType::INTEGER,         // horizon
                        LogicalType::LIST(LogicalType::TIMESTAMP),  // training_end_times
                        LogicalType::ANY},            // params MAP
                       nullptr,                       // main function (unused for in-out)
                       TsCvSplitNativeBind,
                       TsCvSplitNativeInitGlobal,
                       TsCvSplitNativeInitLocal);

    // Set up as table-in-out function
    func.in_out_function = TsCvSplitNativeInOut;
    func.in_out_function_final = TsCvSplitNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
