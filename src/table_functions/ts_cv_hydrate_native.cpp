#include "ts_cv_hydrate_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For DateColumnType, helper functions
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <vector>
#include <set>
#include <mutex>
#include <atomic>
#include <thread>

namespace duckdb {

// ============================================================================
// _ts_cv_hydrate_native - Native CV hydration with unknown features as columns
//
// Takes pre-joined CV folds + source JSON and outputs unknown features as
// actual columns (not a MAP). Applies masking automatically:
// - Train rows: actual values from source JSON
// - Test rows: filled values per strategy (last_value, null, default)
//
// Input (from SQL wrapper):
//   Table with: group, date, target, fold_id, split, __src_json (JSON of source row)
//
// Parameters:
//   - unknown_features: VARCHAR[] of feature column names to extract from JSON
//   - params MAP: {strategy, fill_value}
//
// Output: cv_folds columns (group, date, target, fold_id, split) + unknown features as VARCHAR
// ============================================================================

// ============================================================================
// Bind Data
// ============================================================================

struct TsCvHydrateNativeBindData : public TableFunctionData {
    // Parameters
    string strategy = "last_value";  // last_value, null, default
    string fill_value = "";

    // Unknown features to extract from JSON
    vector<string> unknown_feature_names;

    // CV folds column info (first 5 columns)
    LogicalType group_type;
    LogicalType date_type;
    LogicalType target_type;
    string group_col_name;
    string date_col_name;
    string target_col_name;
};

// ============================================================================
// Standalone data structs (moved out of LocalState for parallel safety)
// ============================================================================

struct TsCvHydrateRow {
    Value group;
    Value date;
    Value target;
    int64_t fold_id;
    string split;
    int64_t date_micros;  // For sorting
    vector<string> unknown_values;  // Values for each unknown feature (as strings)
};

// ============================================================================
// Global State - holds all mutable data storage for thread safety
// ============================================================================

struct TsCvHydrateNativeGlobalState : public GlobalTableFunctionState {
    // All data storage (protected by mutex)
    std::mutex groups_mutex;
    vector<TsCvHydrateRow> rows;

    // Last known values per (group, fold) for each unknown feature
    // Key: "group_key|fold_id" -> vector of string values (one per unknown feature)
    std::map<string, vector<string>> last_known;

    // Output buffer (after processing)
    vector<TsCvHydrateRow> output;
    idx_t offset = 0;
    bool processed = false;

    // Finalize coordination
    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};

    idx_t MaxThreads() const override { return 1; }
};

// ============================================================================
// Local State - minimal per-thread tracking
// ============================================================================

struct TsCvHydrateNativeLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool registered_collector = false;
    bool registered_finalizer = false;
};

// ============================================================================
// Helper Functions
// ============================================================================

static string GetGroupKeyForHydrate(const Value &val) {
    if (val.IsNull()) {
        return "__NULL__";
    }
    return val.ToString();
}

static vector<string> ExtractListStringsForHydrate(const Value &list_val) {
    vector<string> result;
    if (list_val.IsNull() || list_val.type().id() != LogicalTypeId::LIST) {
        return result;
    }
    auto &list_children = ListValue::GetChildren(list_val);
    for (auto &item : list_children) {
        if (!item.IsNull()) {
            result.push_back(item.ToString());
        }
    }
    return result;
}

static string ParseStringParamForHydrate(const Value &params, const string &key, const string &default_val) {
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

// Detect date column type from logical type
static DateColumnType DetectDateColType(const LogicalType &type) {
    switch (type.id()) {
        case LogicalTypeId::DATE:
            return DateColumnType::DATE;
        case LogicalTypeId::TIMESTAMP:
        case LogicalTypeId::TIMESTAMP_TZ:
            return DateColumnType::TIMESTAMP;
        case LogicalTypeId::INTEGER:
            return DateColumnType::INTEGER;
        case LogicalTypeId::BIGINT:
            return DateColumnType::BIGINT;
        default:
            return DateColumnType::TIMESTAMP;
    }
}

// Convert date Value to microseconds for sorting
static int64_t DateValueToMicros(const Value &date_val, DateColumnType date_type) {
    if (date_val.IsNull()) return 0;

    switch (date_type) {
        case DateColumnType::DATE:
            return DateToMicroseconds(date_val.GetValue<date_t>());
        case DateColumnType::TIMESTAMP: {
            int64_t micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
            // Truncate to seconds for consistency
            constexpr int64_t MICROS_PER_SECOND = 1000000;
            return (micros / MICROS_PER_SECOND) * MICROS_PER_SECOND;
        }
        case DateColumnType::INTEGER:
            return date_val.GetValue<int32_t>();
        case DateColumnType::BIGINT:
            return date_val.GetValue<int64_t>();
    }
    return 0;
}

// Extract a value from a JSON string by key
// Returns empty string if not found or null
static string ExtractJsonValue(const string &json_str, const string &key) {
    // Simple JSON extraction - look for "key": value pattern
    // This handles basic cases; DuckDB's json_extract would be more robust
    // but we're working with the raw string here

    // Look for "key":
    string search_key = "\"" + key + "\":";
    size_t pos = json_str.find(search_key);
    if (pos == string::npos) {
        return "";
    }

    pos += search_key.length();

    // Skip whitespace
    while (pos < json_str.length() && (json_str[pos] == ' ' || json_str[pos] == '\t')) {
        pos++;
    }

    if (pos >= json_str.length()) {
        return "";
    }

    // Check for null
    if (json_str.substr(pos, 4) == "null") {
        return "";
    }

    // Check if it's a string value (starts with quote)
    if (json_str[pos] == '"') {
        pos++;  // Skip opening quote
        size_t end = pos;
        while (end < json_str.length() && json_str[end] != '"') {
            if (json_str[end] == '\\' && end + 1 < json_str.length()) {
                end += 2;  // Skip escaped character
            } else {
                end++;
            }
        }
        return json_str.substr(pos, end - pos);
    }

    // It's a number, boolean, or other value - read until comma, }, or ]
    size_t end = pos;
    while (end < json_str.length() &&
           json_str[end] != ',' &&
           json_str[end] != '}' &&
           json_str[end] != ']') {
        end++;
    }

    string value = json_str.substr(pos, end - pos);
    // Trim whitespace
    StringUtil::Trim(value);
    return value;
}

// ============================================================================
// Parameter Validation
// ============================================================================

static const std::set<string> VALID_HYDRATE_PARAMS = {
    "strategy", "fill_value"
};

static void ValidateHydrateParams(const Value &params) {
    if (params.IsNull()) return;

    vector<string> unknown_keys;

    if (params.type().id() == LogicalTypeId::MAP) {
        auto &map_children = MapValue::GetChildren(params);
        for (auto &child : map_children) {
            auto &kv = StructValue::GetChildren(child);
            if (kv.size() >= 2 && !kv[0].IsNull()) {
                auto key = kv[0].ToString();
                StringUtil::Trim(key);
                if (VALID_HYDRATE_PARAMS.find(StringUtil::Lower(key)) == VALID_HYDRATE_PARAMS.end()) {
                    unknown_keys.push_back(key);
                }
            }
        }
    } else if (params.type().id() == LogicalTypeId::STRUCT) {
        auto &struct_type = StructType::GetChildTypes(params.type());
        for (idx_t i = 0; i < struct_type.size(); i++) {
            auto key = struct_type[i].first;
            StringUtil::Trim(key);
            if (VALID_HYDRATE_PARAMS.find(StringUtil::Lower(key)) == VALID_HYDRATE_PARAMS.end()) {
                unknown_keys.push_back(key);
            }
        }
    }

    if (!unknown_keys.empty()) {
        string unknown_list;
        for (size_t i = 0; i < unknown_keys.size(); i++) {
            if (i > 0) unknown_list += ", ";
            unknown_list += "'" + unknown_keys[i] + "'";
        }
        throw InvalidInputException(
            "ts_cv_hydrate_by: Unknown parameter(s): %s\n\n"
            "Available parameters:\n"
            "  - strategy (VARCHAR, default 'last_value'): 'last_value', 'null', or 'default'\n"
            "  - fill_value (VARCHAR, default ''): Value to use when strategy='default'",
            unknown_list.c_str());
    }
}

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsCvHydrateNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsCvHydrateNativeBindData>();

    // Input layout from SQL wrapper:
    // Columns 0-4: group, date, target, fold_id, split (cv_folds columns)
    // Column 5: __src_json (JSON string of source row)
    //
    // Parameters:
    // input.inputs[0] is TABLE
    // input.inputs[1] is unknown_features (VARCHAR[])
    // input.inputs[2] is params (MAP)

    if (input.input_table_types.size() < 6) {
        throw InvalidInputException(
            "_ts_cv_hydrate_native requires 6 columns (cv_folds columns + __src_json). Got %zu columns.",
            input.input_table_types.size());
    }

    // Get types from cv_folds columns (positions 0-4)
    bind_data->group_type = input.input_table_types[0];
    bind_data->date_type = input.input_table_types[1];
    bind_data->target_type = input.input_table_types[2];

    // Get column names
    bind_data->group_col_name = input.input_table_names.size() > 0 ? input.input_table_names[0] : "group_col";
    bind_data->date_col_name = input.input_table_names.size() > 1 ? input.input_table_names[1] : "date_col";
    bind_data->target_col_name = input.input_table_names.size() > 2 ? input.input_table_names[2] : "target_col";

    // Parse unknown_features array (input.inputs[1])
    if (input.inputs.size() >= 2 && !input.inputs[1].IsNull()) {
        bind_data->unknown_feature_names = ExtractListStringsForHydrate(input.inputs[1]);
    }

    if (bind_data->unknown_feature_names.empty()) {
        throw InvalidInputException(
            "_ts_cv_hydrate_native: unknown_features array cannot be empty");
    }

    // Parse params MAP (input.inputs[2])
    if (input.inputs.size() >= 3 && !input.inputs[2].IsNull()) {
        auto &params = input.inputs[2];
        ValidateHydrateParams(params);
        bind_data->strategy = ParseStringParamForHydrate(params, "strategy", "last_value");
        bind_data->fill_value = ParseStringParamForHydrate(params, "fill_value", "");
    }

    // Validate strategy
    auto strategy_lower = StringUtil::Lower(bind_data->strategy);
    if (strategy_lower != "last_value" && strategy_lower != "null" && strategy_lower != "default") {
        throw InvalidInputException(
            "_ts_cv_hydrate_native: invalid strategy '%s'. Must be 'last_value', 'null', or 'default'",
            bind_data->strategy.c_str());
    }
    bind_data->strategy = strategy_lower;

    // Build output schema: cv_folds columns + unknown features as VARCHAR
    // Column 0: group
    return_types.push_back(bind_data->group_type);
    names.push_back(bind_data->group_col_name);

    // Column 1: date
    return_types.push_back(bind_data->date_type);
    names.push_back(bind_data->date_col_name);

    // Column 2: target
    return_types.push_back(bind_data->target_type);
    names.push_back(bind_data->target_col_name);

    // Column 3: fold_id
    return_types.push_back(LogicalType::BIGINT);
    names.push_back("fold_id");

    // Column 4: split
    return_types.push_back(LogicalType::VARCHAR);
    names.push_back("split");

    // Columns 5+: unknown features as VARCHAR
    for (const auto &feat_name : bind_data->unknown_feature_names) {
        return_types.push_back(LogicalType::VARCHAR);
        names.push_back(feat_name);
    }

    return std::move(bind_data);
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsCvHydrateNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<TsCvHydrateNativeGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsCvHydrateNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsCvHydrateNativeLocalState>();
}

// ============================================================================
// In-Out Function - buffer all input rows
// ============================================================================

static OperatorResultType TsCvHydrateNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data.bind_data->Cast<TsCvHydrateNativeBindData>();
    auto &gstate = data.global_state->Cast<TsCvHydrateNativeGlobalState>();
    auto &lstate = data.local_state->Cast<TsCvHydrateNativeLocalState>();

    if (!lstate.registered_collector) {
        gstate.threads_collecting.fetch_add(1);
        lstate.registered_collector = true;
    }

    DateColumnType date_col_type = DetectDateColType(bind_data.date_type);

    // Extract batch locally first
    vector<TsCvHydrateRow> local_batch;

    // Buffer input rows
    for (idx_t row_idx = 0; row_idx < input.size(); row_idx++) {
        // Read cv_folds columns (0-4)
        Value group_val = input.GetValue(0, row_idx);
        Value date_val = input.GetValue(1, row_idx);
        Value target_val = input.GetValue(2, row_idx);
        Value fold_id_val = input.GetValue(3, row_idx);
        Value split_val = input.GetValue(4, row_idx);

        // Read JSON column (5)
        Value json_val = input.GetValue(5, row_idx);
        string json_str = json_val.IsNull() ? "{}" : json_val.ToString();

        // Skip rows with null date (shouldn't happen after join, but be safe)
        if (date_val.IsNull()) continue;

        TsCvHydrateRow row;
        row.group = group_val;
        row.date = date_val;
        row.target = target_val;
        row.fold_id = fold_id_val.IsNull() ? 0 : fold_id_val.GetValue<int64_t>();
        row.split = split_val.IsNull() ? "" : split_val.ToString();
        row.date_micros = DateValueToMicros(date_val, date_col_type);

        // Extract unknown feature values from JSON
        for (const auto &feat_name : bind_data.unknown_feature_names) {
            string val = ExtractJsonValue(json_str, feat_name);
            row.unknown_values.push_back(val);
        }

        local_batch.push_back(std::move(row));
    }

    // Insert into global state under lock
    {
        std::lock_guard<std::mutex> lock(gstate.groups_mutex);
        for (auto &row : local_batch) {
            gstate.rows.push_back(std::move(row));
        }
    }

    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - process and output with masking
// ============================================================================

static OperatorFinalizeResultType TsCvHydrateNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &output) {

    auto &bind_data = data.bind_data->Cast<TsCvHydrateNativeBindData>();
    auto &gstate = data.global_state->Cast<TsCvHydrateNativeGlobalState>();
    auto &lstate = data.local_state->Cast<TsCvHydrateNativeLocalState>();

    // Barrier + claim: ensure all collecting threads are done before processing
    if (!lstate.registered_finalizer) {
        if (lstate.registered_collector)
            gstate.threads_done_collecting.fetch_add(1);
        lstate.registered_finalizer = true;
    }
    if (!lstate.owns_finalize) {
        bool expected = false;
        if (!gstate.finalize_claimed.compare_exchange_strong(expected, true))
            return OperatorFinalizeResultType::FINISHED;
        lstate.owns_finalize = true;
        while (gstate.threads_done_collecting.load() < gstate.threads_collecting.load())
            std::this_thread::yield();
    }

    if (!gstate.processed) {
        // Step 1: Sort rows by (group, fold_id, date)
        std::sort(gstate.rows.begin(), gstate.rows.end(),
            [](const TsCvHydrateRow &a, const TsCvHydrateRow &b) {
                string key_a = GetGroupKeyForHydrate(a.group) + "|" + std::to_string(a.fold_id);
                string key_b = GetGroupKeyForHydrate(b.group) + "|" + std::to_string(b.fold_id);
                if (key_a != key_b) return key_a < key_b;
                return a.date_micros < b.date_micros;
            });

        // Step 2: Process rows, tracking last_known for train rows
        idx_t num_features = bind_data.unknown_feature_names.size();

        for (auto &row : gstate.rows) {
            string key = GetGroupKeyForHydrate(row.group) + "|" + std::to_string(row.fold_id);

            TsCvHydrateRow out_row;
            out_row.group = row.group;
            out_row.date = row.date;
            out_row.target = row.target;
            out_row.fold_id = row.fold_id;
            out_row.split = row.split;
            out_row.date_micros = row.date_micros;

            if (row.split == "train") {
                // Train rows: use actual values and update last_known
                out_row.unknown_values = row.unknown_values;
                gstate.last_known[key] = row.unknown_values;
            } else {
                // Test rows: apply masking strategy
                out_row.unknown_values.resize(num_features);

                if (bind_data.strategy == "null") {
                    // All test values become empty (will be output as NULL)
                    for (idx_t i = 0; i < num_features; i++) {
                        out_row.unknown_values[i] = "";
                    }
                } else if (bind_data.strategy == "default") {
                    // All test values become fill_value
                    for (idx_t i = 0; i < num_features; i++) {
                        out_row.unknown_values[i] = bind_data.fill_value;
                    }
                } else {
                    // last_value: use last known from train
                    auto it = gstate.last_known.find(key);
                    if (it != gstate.last_known.end() && it->second.size() == num_features) {
                        out_row.unknown_values = it->second;
                    } else {
                        // No last_known available, use empty (will be NULL)
                        for (idx_t i = 0; i < num_features; i++) {
                            out_row.unknown_values[i] = "";
                        }
                    }
                }
            }

            gstate.output.push_back(std::move(out_row));
        }

        // Clear input rows to free memory
        gstate.rows.clear();
        gstate.last_known.clear();
        gstate.processed = true;
    }

    // Stream output
    output.Reset();
    idx_t output_idx = 0;

    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    idx_t num_features = bind_data.unknown_feature_names.size();

    while (gstate.offset < gstate.output.size() && output_idx < STANDARD_VECTOR_SIZE) {
        auto &row = gstate.output[gstate.offset];

        // cv_folds columns
        output.SetValue(0, output_idx, row.group);
        output.SetValue(1, output_idx, row.date);
        output.SetValue(2, output_idx, row.target);
        output.SetValue(3, output_idx, Value::BIGINT(row.fold_id));
        output.SetValue(4, output_idx, Value(row.split));

        // Unknown feature columns (as VARCHAR)
        for (idx_t i = 0; i < num_features; i++) {
            if (row.unknown_values[i].empty() && bind_data.strategy == "null" && row.split != "train") {
                // For null strategy on test rows, output actual NULL
                output.SetValue(5 + i, output_idx, Value());
            } else if (row.unknown_values[i].empty()) {
                // Empty value from JSON extraction - could be NULL or empty string
                // Output as NULL for consistency
                output.SetValue(5 + i, output_idx, Value());
            } else {
                output.SetValue(5 + i, output_idx, Value(row.unknown_values[i]));
            }
        }

        output_idx++;
        gstate.offset++;
    }

    output.SetCardinality(output_idx);

    if (gstate.offset >= gstate.output.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }
    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsCvHydrateNativeFunction(ExtensionLoader &loader) {
    // Table-in-out function: (TABLE, unknown_features, params)
    // Input table: pre-joined cv_folds + __src_json column
    // Columns: group, date, target, fold_id, split, __src_json
    TableFunction func(
        "_ts_cv_hydrate_native",
        {LogicalType::TABLE,                    // Input table (pre-joined)
         LogicalType::LIST(LogicalType::VARCHAR),  // unknown_features
         LogicalType::ANY},                     // params (MAP or STRUCT)
        nullptr,
        TsCvHydrateNativeBind,
        TsCvHydrateNativeInitGlobal,
        TsCvHydrateNativeInitLocal);

    func.in_out_function = TsCvHydrateNativeInOut;
    func.in_out_function_final = TsCvHydrateNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
