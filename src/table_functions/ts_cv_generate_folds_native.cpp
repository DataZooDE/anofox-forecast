#include "ts_cv_generate_folds_native.hpp"
#include "ts_fill_gaps_native.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <set>

namespace duckdb {

// ============================================================================
// _ts_cv_generate_folds_native - Native position-based fold boundary generator
//
// This function uses position-based indexing (not date arithmetic) to compute
// fold boundaries. This correctly handles all frequency types including
// calendar-based frequencies (monthly, quarterly, yearly).
//
// ASSUMPTION: Input data is pre-cleaned with no gaps and consistent frequency.
//
// Returns a LIST of training end dates (preserving the original date type).
// ============================================================================

// ============================================================================
// Bind Data
// ============================================================================

struct TsCvGenerateFoldsBindData : public TableFunctionData {
    int64_t n_folds = 3;
    int64_t horizon = 7;
    int64_t initial_train_size = -1;  // -1 means auto (n_dates - n_folds * horizon)
    int64_t skip_length = -1;         // -1 means horizon
    bool clip_horizon = false;

    // Type preservation
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
};

// ============================================================================
// Local State
// ============================================================================

struct TsCvGenerateFoldsLocalState : public LocalTableFunctionState {
    std::set<int64_t> unique_dates;
    bool has_output = false;
};

// ============================================================================
// Global State
// ============================================================================

struct TsCvGenerateFoldsGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override { return 1; }
};

// ============================================================================
// Helper functions
// ============================================================================

static int64_t ParseInt64FromParams(const Value &params, const string &key, int64_t default_val) {
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

static bool ParseBoolFromParams(const Value &params, const string &key, bool default_val) {
    if (params.IsNull()) return default_val;

    if (params.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params);
        for (auto &child : map_children) {
            auto &kv = StructValue::GetChildren(child);
            if (kv.size() >= 2 && !kv[0].IsNull()) {
                auto k = kv[0].ToString();
                StringUtil::Trim(k);
                if (StringUtil::Lower(k) == StringUtil::Lower(key) && !kv[1].IsNull()) {
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

static unique_ptr<FunctionData> TsCvGenerateFoldsBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsCvGenerateFoldsBindData>();

    // Validate input table has exactly 1 column (date_col)
    if (input.input_table_types.size() != 1) {
        throw InvalidInputException(
            "_ts_cv_generate_folds_native requires input with exactly 1 column: date_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Detect and store date column type
    auto &date_type = input.input_table_types[0];
    if (date_type.id() == LogicalTypeId::DATE) {
        bind_data->date_col_type = DateColumnType::DATE;
        bind_data->date_logical_type = LogicalType::DATE;
    } else if (date_type.id() == LogicalTypeId::TIMESTAMP) {
        bind_data->date_col_type = DateColumnType::TIMESTAMP;
        bind_data->date_logical_type = LogicalType::TIMESTAMP;
    } else if (date_type.id() == LogicalTypeId::INTEGER) {
        bind_data->date_col_type = DateColumnType::INTEGER;
        bind_data->date_logical_type = LogicalType::INTEGER;
    } else if (date_type.id() == LogicalTypeId::BIGINT) {
        bind_data->date_col_type = DateColumnType::BIGINT;
        bind_data->date_logical_type = LogicalType::BIGINT;
    } else {
        bind_data->date_col_type = DateColumnType::TIMESTAMP;
        bind_data->date_logical_type = LogicalType::TIMESTAMP;
    }

    // Parse positional arguments: n_folds, horizon
    if (input.inputs.size() >= 2) {
        bind_data->n_folds = input.inputs[1].GetValue<int64_t>();
    }
    if (input.inputs.size() >= 3) {
        bind_data->horizon = input.inputs[2].GetValue<int64_t>();
    }

    // Parse optional params (index 3)
    if (input.inputs.size() >= 4 && !input.inputs[3].IsNull()) {
        auto &params = input.inputs[3];
        bind_data->initial_train_size = ParseInt64FromParams(params, "initial_train_size", -1);
        bind_data->skip_length = ParseInt64FromParams(params, "skip_length", -1);
        bind_data->clip_horizon = ParseBoolFromParams(params, "clip_horizon", false);
    }

    // Output: LIST of training end dates (preserving original type)
    names.push_back("training_end_times");
    return_types.push_back(LogicalType::LIST(bind_data->date_logical_type));

    return std::move(bind_data);
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsCvGenerateFoldsInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<TsCvGenerateFoldsGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsCvGenerateFoldsInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsCvGenerateFoldsLocalState>();
}

// ============================================================================
// In-Out Function - collect unique dates
// ============================================================================

static OperatorResultType TsCvGenerateFoldsInOut(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data.bind_data->Cast<TsCvGenerateFoldsBindData>();
    auto &local_state = data.local_state->Cast<TsCvGenerateFoldsLocalState>();

    // Process input rows - collect unique dates
    auto &date_vec = input.data[0];

    for (idx_t i = 0; i < input.size(); i++) {
        auto date_val = date_vec.GetValue(i);
        if (date_val.IsNull()) continue;

        int64_t date_micros;
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                date_micros = DateToMicroseconds(date_val.GetValue<date_t>());
                break;
            case DateColumnType::TIMESTAMP: {
                date_micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
                // Truncate to seconds
                constexpr int64_t MICROS_PER_SECOND = 1000000;
                date_micros = (date_micros / MICROS_PER_SECOND) * MICROS_PER_SECOND;
                break;
            }
            case DateColumnType::INTEGER:
                date_micros = date_val.GetValue<int32_t>();
                break;
            case DateColumnType::BIGINT:
                date_micros = date_val.GetValue<int64_t>();
                break;
        }

        local_state.unique_dates.insert(date_micros);
    }

    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - compute fold boundaries and output
// ============================================================================

static OperatorFinalizeResultType TsCvGenerateFoldsFinalize(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &output) {

    auto &bind_data = data.bind_data->Cast<TsCvGenerateFoldsBindData>();
    auto &local_state = data.local_state->Cast<TsCvGenerateFoldsLocalState>();

    if (local_state.has_output) {
        return OperatorFinalizeResultType::FINISHED;
    }
    local_state.has_output = true;

    // Convert set to sorted vector
    vector<int64_t> sorted_dates(local_state.unique_dates.begin(), local_state.unique_dates.end());
    idx_t n_dates = sorted_dates.size();

    if (n_dates < 2) {
        // Return empty list
        output.data[0].SetVectorType(VectorType::FLAT_VECTOR);
        auto list_data = ListVector::GetData(output.data[0]);
        list_data[0].offset = 0;
        list_data[0].length = 0;
        output.SetCardinality(1);
        return OperatorFinalizeResultType::FINISHED;
    }

    // Compute initial train size
    idx_t init_train_size;
    if (bind_data.initial_train_size > 0) {
        init_train_size = static_cast<idx_t>(bind_data.initial_train_size);
    } else {
        // Default: position folds so last fold ends at data end
        idx_t folds = static_cast<idx_t>(bind_data.n_folds);
        idx_t horizon = static_cast<idx_t>(bind_data.horizon);
        idx_t needed = horizon * folds;
        init_train_size = (n_dates > needed) ? (n_dates - needed) : 1;
    }

    // Compute skip length
    idx_t skip_length = bind_data.skip_length > 0
        ? static_cast<idx_t>(bind_data.skip_length)
        : static_cast<idx_t>(bind_data.horizon);

    idx_t horizon = static_cast<idx_t>(bind_data.horizon);

    // Generate fold boundaries
    vector<int64_t> training_end_dates;

    for (int64_t fold = 0; fold < bind_data.n_folds; fold++) {
        // Training end index (inclusive)
        idx_t train_end_idx = init_train_size - 1 + fold * skip_length;

        // Test end index (inclusive)
        idx_t test_end_idx = train_end_idx + horizon;

        // Check if fold is valid
        bool valid = bind_data.clip_horizon
            ? (train_end_idx + 1 < n_dates)  // At least 1 test point
            : (test_end_idx < n_dates);       // Full horizon fits

        if (!valid) break;

        // Training end date is at train_end_idx
        if (train_end_idx < n_dates) {
            training_end_dates.push_back(sorted_dates[train_end_idx]);
        }
    }

    // Build output LIST
    output.data[0].SetVectorType(VectorType::FLAT_VECTOR);
    auto &list_vec = output.data[0];
    auto list_data = ListVector::GetData(list_vec);
    auto &child_vec = ListVector::GetEntry(list_vec);

    ListVector::Reserve(list_vec, training_end_dates.size());
    ListVector::SetListSize(list_vec, training_end_dates.size());

    list_data[0].offset = 0;
    list_data[0].length = training_end_dates.size();

    // Fill child vector with dates
    for (idx_t i = 0; i < training_end_dates.size(); i++) {
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                child_vec.SetValue(i, Value::DATE(MicrosecondsToDate(training_end_dates[i])));
                break;
            case DateColumnType::TIMESTAMP:
                child_vec.SetValue(i, Value::TIMESTAMP(MicrosecondsToTimestamp(training_end_dates[i])));
                break;
            case DateColumnType::INTEGER:
                child_vec.SetValue(i, Value::INTEGER(static_cast<int32_t>(training_end_dates[i])));
                break;
            case DateColumnType::BIGINT:
                child_vec.SetValue(i, Value::BIGINT(training_end_dates[i]));
                break;
        }
    }

    output.SetCardinality(1);
    return OperatorFinalizeResultType::FINISHED;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsCvGenerateFoldsNativeFunction(ExtensionLoader &loader) {
    // Table-in-out function: (TABLE, n_folds, horizon, params)
    // Input table must have 1 column: date_col
    // NOTE: No frequency parameter needed - uses position-based indexing
    //       Assumes pre-cleaned data with no gaps
    TableFunction func(
        "_ts_cv_generate_folds_native",
        {LogicalType::TABLE,
         LogicalType::BIGINT,   // n_folds
         LogicalType::BIGINT,   // horizon
         LogicalType::ANY},     // params (MAP or STRUCT)
        nullptr,                // No execute function - use in_out_function
        TsCvGenerateFoldsBind,
        TsCvGenerateFoldsInitGlobal,
        TsCvGenerateFoldsInitLocal);

    func.in_out_function = TsCvGenerateFoldsInOut;
    func.in_out_function_final = TsCvGenerateFoldsFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
