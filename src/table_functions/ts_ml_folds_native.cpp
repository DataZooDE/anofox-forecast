#include "ts_ml_folds_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For DateColumnType, helper functions
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <vector>
#include <set>

namespace duckdb {

// ============================================================================
// _ts_ml_folds_native - Native ML-ready train/test fold generator
//
// This function combines fold boundary generation and train/test splitting
// in a single native function, suitable for ML model backtesting.
//
// Unlike ts_cv_split_by which requires pre-computed training_end_times,
// this function automatically computes fold boundaries from the data itself,
// avoiding the DuckDB "only one subquery parameter" limitation.
//
// ASSUMPTION: Input data is pre-cleaned with no gaps and consistent frequency.
//
// Parameters:
//   - source table: (group_col, date_col, target_col)
//   - n_folds: number of folds to generate
//   - horizon: number of periods in test window
//   - params MAP: {gap, embargo, window_type, min_train_size, initial_train_size,
//                  skip_length, clip_horizon}
//
// Output: (group_col, date_col, target_col, fold_id, split)
// ============================================================================

// ============================================================================
// Bind Data
// ============================================================================

struct TsMlFoldsBindData : public TableFunctionData {
    // Fold generation parameters
    int64_t n_folds = 3;
    int64_t horizon = 7;
    int64_t initial_train_size = -1;  // -1 means auto
    int64_t skip_length = -1;         // -1 means horizon
    bool clip_horizon = false;

    // Split parameters
    int64_t gap = 0;
    int64_t embargo = 0;
    string window_type = "expanding";
    int64_t min_train_size = 1;

    // Type preservation
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
    LogicalType value_logical_type = LogicalType(LogicalTypeId::DOUBLE);
};

// ============================================================================
// Local State
// ============================================================================

struct TsMlFoldsLocalState : public LocalTableFunctionState {
    // Buffered input data
    struct InputRow {
        Value group_val;
        Value date_val;
        int64_t date_micros;
        double value;
        bool valid;
        string group_key;
    };
    vector<InputRow> input_rows;

    // Per-group sorted indices
    std::map<string, vector<idx_t>> group_sorted_indices;

    // Computed fold boundaries (training end positions per group)
    // Key: group_key, Value: vector of train_end_positions
    std::map<string, vector<idx_t>> group_fold_train_ends;

    // Pre-computed output rows
    struct OutputRow {
        idx_t input_idx;
        int64_t fold_id;
        bool is_train;
    };
    vector<OutputRow> output_rows;

    bool preprocessing_done = false;
    idx_t output_offset = 0;
};

// ============================================================================
// Global State
// ============================================================================

struct TsMlFoldsGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override { return 1; }
};

// ============================================================================
// Helper Functions
// ============================================================================

static string GetGroupKeyForMl(const Value &val) {
    if (val.IsNull()) {
        return "__NULL__";
    }
    return val.ToString();
}

static string ParseStringParamMl(const Value &params, const string &key, const string &default_val) {
    if (params.IsNull()) return default_val;

    if (params.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params);
        for (auto &child : map_children) {
            auto &kv = StructValue::GetChildren(child);
            if (kv.size() >= 2 && !kv[0].IsNull()) {
                auto k = kv[0].ToString();
                StringUtil::Trim(k);
                if (StringUtil::Lower(k) == StringUtil::Lower(key) && !kv[1].IsNull()) {
                    return kv[1].ToString();
                }
            }
        }
    } else if (params.type().id() == LogicalTypeId::STRUCT) {
        auto &struct_children = StructValue::GetChildren(params);
        auto &struct_type = StructType::GetChildTypes(params.type());
        for (idx_t i = 0; i < struct_children.size(); i++) {
            auto k = struct_type[i].first;
            StringUtil::Trim(k);
            if (StringUtil::Lower(k) == StringUtil::Lower(key) && !struct_children[i].IsNull()) {
                return struct_children[i].ToString();
            }
        }
    }
    return default_val;
}

static int64_t ParseInt64ParamMl(const Value &params, const string &key, int64_t default_val) {
    if (params.IsNull()) return default_val;

    if (params.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params);
        for (auto &child : map_children) {
            auto &kv = StructValue::GetChildren(child);
            if (kv.size() >= 2 && !kv[0].IsNull()) {
                auto k = kv[0].ToString();
                StringUtil::Trim(k);
                if (StringUtil::Lower(k) == StringUtil::Lower(key) && !kv[1].IsNull()) {
                    try {
                        return kv[1].GetValue<int64_t>();
                    } catch (...) {
                        try {
                            return std::stoll(kv[1].ToString());
                        } catch (...) {
                            return default_val;
                        }
                    }
                }
            }
        }
    } else if (params.type().id() == LogicalTypeId::STRUCT) {
        auto &struct_children = StructValue::GetChildren(params);
        auto &struct_type = StructType::GetChildTypes(params.type());
        for (idx_t i = 0; i < struct_children.size(); i++) {
            auto k = struct_type[i].first;
            StringUtil::Trim(k);
            if (StringUtil::Lower(k) == StringUtil::Lower(key) && !struct_children[i].IsNull()) {
                try {
                    return struct_children[i].GetValue<int64_t>();
                } catch (...) {
                    try {
                        return std::stoll(struct_children[i].ToString());
                    } catch (...) {
                        return default_val;
                    }
                }
            }
        }
    }
    return default_val;
}

static bool ParseBoolParamMl(const Value &params, const string &key, bool default_val) {
    if (params.IsNull()) return default_val;

    if (params.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params);
        for (auto &child : map_children) {
            auto &kv = StructValue::GetChildren(child);
            if (kv.size() >= 2 && !kv[0].IsNull()) {
                auto k = kv[0].ToString();
                StringUtil::Trim(k);
                if (StringUtil::Lower(k) == StringUtil::Lower(key) && !kv[1].IsNull()) {
                    if (kv[1].type().id() == LogicalTypeId::BOOLEAN) {
                        return kv[1].GetValue<bool>();
                    }
                    auto v = StringUtil::Lower(kv[1].ToString());
                    return v == "true" || v == "1" || v == "yes";
                }
            }
        }
    } else if (params.type().id() == LogicalTypeId::STRUCT) {
        auto &struct_children = StructValue::GetChildren(params);
        auto &struct_type = StructType::GetChildTypes(params.type());
        for (idx_t i = 0; i < struct_children.size(); i++) {
            auto k = struct_type[i].first;
            StringUtil::Trim(k);
            if (StringUtil::Lower(k) == StringUtil::Lower(key) && !struct_children[i].IsNull()) {
                if (struct_children[i].type().id() == LogicalTypeId::BOOLEAN) {
                    return struct_children[i].GetValue<bool>();
                }
                auto v = StringUtil::Lower(struct_children[i].ToString());
                return v == "true" || v == "1" || v == "yes";
            }
        }
    }
    return default_val;
}

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsMlFoldsBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsMlFoldsBindData>();

    // Validate input table has 3 columns (group, date, value)
    if (input.input_table_types.size() != 3) {
        throw InvalidInputException(
            "_ts_ml_folds_native requires input with exactly 3 columns: group_col, date_col, target_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Detect column types
    bind_data->group_logical_type = input.input_table_types[0];
    bind_data->date_logical_type = input.input_table_types[1];
    bind_data->value_logical_type = input.input_table_types[2];

    auto &date_type = input.input_table_types[1];
    if (date_type.id() == LogicalTypeId::DATE) {
        bind_data->date_col_type = DateColumnType::DATE;
    } else if (date_type.id() == LogicalTypeId::TIMESTAMP) {
        bind_data->date_col_type = DateColumnType::TIMESTAMP;
    } else if (date_type.id() == LogicalTypeId::INTEGER) {
        bind_data->date_col_type = DateColumnType::INTEGER;
    } else if (date_type.id() == LogicalTypeId::BIGINT) {
        bind_data->date_col_type = DateColumnType::BIGINT;
    } else {
        bind_data->date_col_type = DateColumnType::TIMESTAMP;
    }

    // Parse positional arguments: n_folds, horizon
    if (input.inputs.size() >= 2 && !input.inputs[1].IsNull()) {
        bind_data->n_folds = input.inputs[1].GetValue<int64_t>();
    }
    if (input.inputs.size() >= 3 && !input.inputs[2].IsNull()) {
        bind_data->horizon = input.inputs[2].GetValue<int64_t>();
    }

    // Parse optional params MAP (index 3)
    if (input.inputs.size() >= 4 && !input.inputs[3].IsNull()) {
        auto &params = input.inputs[3];
        bind_data->initial_train_size = ParseInt64ParamMl(params, "initial_train_size", -1);
        bind_data->skip_length = ParseInt64ParamMl(params, "skip_length", -1);
        bind_data->clip_horizon = ParseBoolParamMl(params, "clip_horizon", false);
        bind_data->gap = ParseInt64ParamMl(params, "gap", 0);
        bind_data->embargo = ParseInt64ParamMl(params, "embargo", 0);
        bind_data->window_type = ParseStringParamMl(params, "window_type", "expanding");
        bind_data->min_train_size = ParseInt64ParamMl(params, "min_train_size", 1);
    }

    // Output columns: preserve original column names, add fold_id and split
    auto &table_names = input.input_table_names;

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

static unique_ptr<GlobalTableFunctionState> TsMlFoldsInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<TsMlFoldsGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsMlFoldsInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsMlFoldsLocalState>();
}

// ============================================================================
// In-Out Function - buffer all input rows
// ============================================================================

static OperatorResultType TsMlFoldsInOut(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data.bind_data->Cast<TsMlFoldsBindData>();
    auto &local_state = data.local_state->Cast<TsMlFoldsLocalState>();

    // Buffer input rows
    for (idx_t row_idx = 0; row_idx < input.size(); row_idx++) {
        Value group_val = input.GetValue(0, row_idx);
        Value date_val = input.GetValue(1, row_idx);
        Value value_val = input.GetValue(2, row_idx);

        if (date_val.IsNull()) continue;

        TsMlFoldsLocalState::InputRow row;
        row.group_val = group_val;
        row.date_val = date_val;
        row.group_key = GetGroupKeyForMl(group_val);

        // Convert date to microseconds
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                row.date_micros = DateToMicroseconds(date_val.GetValue<date_t>());
                break;
            case DateColumnType::TIMESTAMP: {
                row.date_micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
                // Truncate to seconds for consistency
                constexpr int64_t MICROS_PER_SECOND = 1000000;
                row.date_micros = (row.date_micros / MICROS_PER_SECOND) * MICROS_PER_SECOND;
                break;
            }
            case DateColumnType::INTEGER:
                row.date_micros = date_val.GetValue<int32_t>();
                break;
            case DateColumnType::BIGINT:
                row.date_micros = date_val.GetValue<int64_t>();
                break;
        }

        row.value = value_val.IsNull() ? 0.0 : value_val.GetValue<double>();
        row.valid = !value_val.IsNull();

        local_state.input_rows.push_back(std::move(row));
    }

    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - compute folds and output splits
// ============================================================================

static OperatorFinalizeResultType TsMlFoldsFinalize(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &output) {

    auto &bind_data = data.bind_data->Cast<TsMlFoldsBindData>();
    auto &local_state = data.local_state->Cast<TsMlFoldsLocalState>();

    if (!local_state.preprocessing_done) {
        // Step 1: Build group indices and sort by date
        for (idx_t i = 0; i < local_state.input_rows.size(); i++) {
            auto &row = local_state.input_rows[i];
            local_state.group_sorted_indices[row.group_key].push_back(i);
        }

        for (auto &[group_key, indices] : local_state.group_sorted_indices) {
            std::sort(indices.begin(), indices.end(),
                [&local_state](idx_t a, idx_t b) {
                    return local_state.input_rows[a].date_micros < local_state.input_rows[b].date_micros;
                });
        }

        // Step 2: For each group, compute fold train_end positions
        for (auto &[group_key, sorted_indices] : local_state.group_sorted_indices) {
            idx_t n_points = sorted_indices.size();
            if (n_points < 2) continue;

            // Compute initial_train_size
            idx_t init_train_size;
            if (bind_data.initial_train_size > 0) {
                init_train_size = static_cast<idx_t>(bind_data.initial_train_size);
            } else {
                // Default: position folds so last fold ends at data end
                idx_t folds = static_cast<idx_t>(bind_data.n_folds);
                idx_t horizon = static_cast<idx_t>(bind_data.horizon);
                idx_t needed = horizon * folds;
                init_train_size = (n_points > needed) ? (n_points - needed) : 1;
            }

            // Compute skip_length
            idx_t skip_length = bind_data.skip_length > 0
                ? static_cast<idx_t>(bind_data.skip_length)
                : static_cast<idx_t>(bind_data.horizon);

            idx_t horizon = static_cast<idx_t>(bind_data.horizon);
            idx_t gap = static_cast<idx_t>(bind_data.gap);

            // Generate fold train_end positions
            vector<idx_t> train_ends;
            for (int64_t fold = 0; fold < bind_data.n_folds; fold++) {
                // Training end position (0-indexed, inclusive)
                idx_t train_end_pos = init_train_size - 1 + fold * skip_length;

                // Test start and end positions
                idx_t test_start_pos = train_end_pos + 1 + gap;
                idx_t test_end_pos = test_start_pos + horizon - 1;

                // Check if fold is valid
                bool valid = bind_data.clip_horizon
                    ? (test_start_pos < n_points)  // At least 1 test point
                    : (test_end_pos < n_points);   // Full horizon fits

                if (!valid) break;

                train_ends.push_back(train_end_pos);
            }

            local_state.group_fold_train_ends[group_key] = std::move(train_ends);
        }

        // Step 3: Generate output rows for each group and fold
        for (auto &[group_key, sorted_indices] : local_state.group_sorted_indices) {
            idx_t n_points = sorted_indices.size();

            auto it = local_state.group_fold_train_ends.find(group_key);
            if (it == local_state.group_fold_train_ends.end()) continue;

            auto &train_ends = it->second;

            for (idx_t fold_idx = 0; fold_idx < train_ends.size(); fold_idx++) {
                idx_t train_end_pos = train_ends[fold_idx];
                int64_t fold_id = static_cast<int64_t>(fold_idx + 1);

                idx_t gap = static_cast<idx_t>(bind_data.gap);
                idx_t horizon = static_cast<idx_t>(bind_data.horizon);

                // Compute train start based on window_type
                idx_t train_start_pos = 0;
                if (bind_data.window_type == "fixed" || bind_data.window_type == "sliding") {
                    // Fixed/sliding window: use min_train_size
                    idx_t min_train = static_cast<idx_t>(bind_data.min_train_size);
                    if (train_end_pos + 1 >= min_train) {
                        train_start_pos = train_end_pos + 1 - min_train;
                    }
                }
                // For "expanding", train_start_pos stays at 0

                // Test positions
                idx_t test_start_pos = train_end_pos + 1 + gap;
                idx_t test_end_pos = test_start_pos + horizon - 1;

                // Clip to available data
                if (test_end_pos >= n_points) {
                    test_end_pos = n_points - 1;
                }

                // Generate train rows
                for (idx_t pos = train_start_pos; pos <= train_end_pos && pos < n_points; pos++) {
                    TsMlFoldsLocalState::OutputRow out;
                    out.input_idx = sorted_indices[pos];
                    out.fold_id = fold_id;
                    out.is_train = true;
                    local_state.output_rows.push_back(out);
                }

                // Generate test rows
                for (idx_t pos = test_start_pos; pos <= test_end_pos && pos < n_points; pos++) {
                    TsMlFoldsLocalState::OutputRow out;
                    out.input_idx = sorted_indices[pos];
                    out.fold_id = fold_id;
                    out.is_train = false;
                    local_state.output_rows.push_back(out);
                }
            }
        }

        local_state.preprocessing_done = true;
    }

    // Stream output
    output.Reset();
    idx_t output_idx = 0;

    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    while (local_state.output_offset < local_state.output_rows.size() && output_idx < STANDARD_VECTOR_SIZE) {
        auto &out_row = local_state.output_rows[local_state.output_offset];
        auto &input_row = local_state.input_rows[out_row.input_idx];

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
        local_state.output_offset++;
    }

    output.SetCardinality(output_idx);

    if (local_state.output_offset >= local_state.output_rows.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }
    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsMlFoldsNativeFunction(ExtensionLoader &loader) {
    // Table-in-out function: (TABLE, n_folds, horizon, params)
    // Input table must have 3 columns: group_col, date_col, target_col
    TableFunction func(
        "_ts_ml_folds_native",
        {LogicalType::TABLE,     // Input table
         LogicalType::BIGINT,    // n_folds
         LogicalType::BIGINT,    // horizon
         LogicalType::ANY},      // params (MAP or STRUCT)
        nullptr,
        TsMlFoldsBind,
        TsMlFoldsInitGlobal,
        TsMlFoldsInitLocal);

    func.in_out_function = TsMlFoldsInOut;
    func.in_out_function_final = TsMlFoldsFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
