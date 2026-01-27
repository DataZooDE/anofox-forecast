#include "ts_cv_split_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For ParseFrequencyToSeconds, etc.
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <vector>

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
// Fold Bounds Structure
// ============================================================================

struct FoldBounds {
    int64_t fold_id;
    int64_t train_end;    // microseconds
    int64_t test_start;   // microseconds
    int64_t test_end;     // microseconds
};

// ============================================================================
// Bind Data - captures all parameters
// ============================================================================

struct TsCvSplitNativeBindData : public TableFunctionData {
    // Fold definitions (computed at bind time)
    vector<FoldBounds> folds;

    // Parameters
    int64_t horizon = 7;
    int64_t frequency_seconds = 86400;
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
// Local State - buffers input and manages output
// ============================================================================

struct TsCvSplitNativeLocalState : public LocalTableFunctionState {
    // Buffered input data
    struct InputRow {
        Value group_val;
        Value date_val;
        int64_t date_micros;
        double value;
        bool valid;
    };
    vector<InputRow> input_rows;

    // Output state
    bool processed = false;
    idx_t current_input_idx = 0;
    idx_t current_fold_idx = 0;
};

// ============================================================================
// Global State
// ============================================================================

struct TsCvSplitNativeGlobalState : public GlobalTableFunctionState {
    idx_t max_threads = 1;

    idx_t MaxThreads() const override {
        return max_threads;
    }
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

    // Parse frequency (parameter index 2)
    if (input.inputs.size() > 2 && !input.inputs[2].IsNull()) {
        string freq_str = input.inputs[2].ToString();
        auto [freq_seconds, is_raw] = ParseFrequencyToSeconds(freq_str);
        bind_data->frequency_seconds = freq_seconds;
    }

    // Parse params MAP (parameter index 4)
    Value params_value;
    if (input.inputs.size() > 4 && !input.inputs[4].IsNull()) {
        params_value = input.inputs[4];
        bind_data->window_type = ParseStringParam(params_value, "window_type", "expanding");
        bind_data->min_train_size = ParseIntParam(params_value, "min_train_size", 1);
        bind_data->gap = ParseIntParam(params_value, "gap", 0);
        bind_data->embargo = ParseIntParam(params_value, "embargo", 0);
    }

    // Parse training_end_times array (parameter index 3)
    if (input.inputs.size() > 3 && !input.inputs[3].IsNull()) {
        auto &training_ends = input.inputs[3];
        if (training_ends.type().id() != LogicalTypeId::LIST) {
            throw InvalidInputException("training_end_times must be an array of timestamps");
        }

        auto &children = ListValue::GetChildren(training_ends);
        int64_t freq_micros = bind_data->frequency_seconds * 1000000;

        // First pass: collect all training end times
        vector<int64_t> train_ends;
        for (auto &child : children) {
            train_ends.push_back(TimestampToMicros(child));
        }

        // Sort training ends
        std::sort(train_ends.begin(), train_ends.end());

        // Compute fold bounds
        for (idx_t i = 0; i < train_ends.size(); i++) {
            FoldBounds fold;
            fold.fold_id = static_cast<int64_t>(i + 1);
            fold.train_end = train_ends[i];

            // test_start = train_end + (gap + 1) * frequency
            fold.test_start = fold.train_end + (bind_data->gap + 1) * freq_micros;

            // test_end = train_end + (gap + horizon) * frequency
            fold.test_end = fold.train_end + (bind_data->gap + bind_data->horizon) * freq_micros;

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
    auto &local_state = data.local_state->Cast<TsCvSplitNativeLocalState>();

    // Buffer all input rows
    for (idx_t row_idx = 0; row_idx < input.size(); row_idx++) {
        Value group_val = input.GetValue(0, row_idx);
        Value date_val = input.GetValue(1, row_idx);
        Value value_val = input.GetValue(2, row_idx);

        if (date_val.IsNull()) {
            continue;
        }

        TsCvSplitNativeLocalState::InputRow row;
        row.group_val = group_val;
        row.date_val = date_val;

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

        local_state.input_rows.push_back(std::move(row));
    }

    // Don't output anything during input phase
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - outputs expanded CV splits
// ============================================================================

static OperatorFinalizeResultType TsCvSplitNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &output) {

    auto &bind_data = data.bind_data->CastNoConst<TsCvSplitNativeBindData>();
    auto &local_state = data.local_state->Cast<TsCvSplitNativeLocalState>();

    output.Reset();
    idx_t output_idx = 0;

    // Initialize all output vectors as FLAT_VECTOR for parallel-safe batch merging
    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    int64_t freq_micros = bind_data.frequency_seconds * 1000000;

    // Continue from where we left off
    while (local_state.current_input_idx < local_state.input_rows.size()) {
        auto &row = local_state.input_rows[local_state.current_input_idx];

        // Process remaining folds for this row
        while (local_state.current_fold_idx < bind_data.folds.size()) {
            auto &fold = bind_data.folds[local_state.current_fold_idx];

            // Compute train_start based on window_type
            int64_t train_start;
            if (bind_data.window_type == "expanding") {
                // For expanding window, train includes all data from the beginning
                train_start = INT64_MIN;
            } else {
                // fixed or sliding window
                train_start = fold.train_end - (bind_data.min_train_size * freq_micros);
            }

            // Handle embargo: adjust train_start if needed
            if (bind_data.embargo > 0 && fold.fold_id > 1) {
                auto &prev_fold = bind_data.folds[fold.fold_id - 2];
                int64_t embargo_cutoff = prev_fold.test_end + (bind_data.embargo * freq_micros);
                if (embargo_cutoff > train_start) {
                    train_start = embargo_cutoff;
                }
            }

            // Check if row is in train or test period
            bool is_train = (row.date_micros >= train_start && row.date_micros <= fold.train_end);
            bool is_test = (row.date_micros >= fold.test_start && row.date_micros <= fold.test_end);

            if (is_train || is_test) {
                // Check if output buffer is full
                if (output_idx >= STANDARD_VECTOR_SIZE) {
                    output.SetCardinality(output_idx);
                    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
                }

                // Output this row for this fold
                output.SetValue(0, output_idx, row.group_val);
                output.SetValue(1, output_idx, row.date_val);

                if (row.valid) {
                    output.SetValue(2, output_idx, Value::DOUBLE(row.value));
                } else {
                    output.SetValue(2, output_idx, Value());
                }

                output.SetValue(3, output_idx, Value::BIGINT(fold.fold_id));
                output.SetValue(4, output_idx, Value(is_train ? "train" : "test"));

                output_idx++;
            }

            local_state.current_fold_idx++;
        }

        // Reset fold index for next input row
        local_state.current_fold_idx = 0;
        local_state.current_input_idx++;
    }

    output.SetCardinality(output_idx);
    return OperatorFinalizeResultType::FINISHED;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsCvSplitNativeFunction(ExtensionLoader &loader) {
    // Create the table function with table input
    TableFunction func("_ts_cv_split_native",
                       {LogicalType::TABLE,           // Input table (group, date, value)
                        LogicalType::INTEGER,         // horizon
                        LogicalType::VARCHAR,         // frequency
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
