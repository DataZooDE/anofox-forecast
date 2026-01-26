#include "ts_combine_keys.hpp"
#include "ts_fill_gaps_native.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <vector>

namespace duckdb {

// ============================================================================
// ts_combine_keys - Native key combination function
//
// Combines multiple ID columns into a single unique_id WITHOUT aggregation.
// Input table format: date_col, value_col, id_col1, id_col2, ...
// Output: unique_id, date_col, value_col
//
// Parameters via MAP{}:
// - separator: Character(s) to join ID parts (default: '|')
// ============================================================================

// ============================================================================
// Bind Data
// ============================================================================

struct TsCombineKeysBindData : public TableFunctionData {
    // Parameters
    string separator = "|";

    // Input schema info
    idx_t num_id_cols = 0;
    string date_col_name = "date";
    string value_col_name = "value";

    // Type preservation
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    LogicalType value_logical_type = LogicalType(LogicalTypeId::DOUBLE);
};

// ============================================================================
// Local State
// ============================================================================

struct TsCombineKeysLocalState : public LocalTableFunctionState {
    // No state needed - direct pass-through
};

// ============================================================================
// Global State
// ============================================================================

struct TsCombineKeysGlobalState : public GlobalTableFunctionState {
    idx_t max_threads = 1;

    idx_t MaxThreads() const override {
        return max_threads;
    }
};

// ============================================================================
// Helper: Extract string from MAP parameter
// ============================================================================

static string ExtractMapString(const Value& map_val, const string& key, const string& default_val) {
    if (map_val.IsNull() || map_val.type().id() != LogicalTypeId::MAP) {
        return default_val;
    }

    auto &map_children = MapValue::GetChildren(map_val);
    for (auto &entry : map_children) {
        auto &key_val = StructValue::GetChildren(entry)[0];
        auto &val_val = StructValue::GetChildren(entry)[1];
        if (!key_val.IsNull() && key_val.ToString() == key) {
            return val_val.IsNull() ? default_val : val_val.ToString();
        }
    }
    return default_val;
}

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsCombineKeysBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsCombineKeysBindData>();

    // Parse MAP{} parameters from named parameter
    for (auto &kv : input.named_parameters) {
        if (kv.first == "params" && !kv.second.IsNull()) {
            bind_data->separator = ExtractMapString(kv.second, "separator", "|");
        }
    }

    // Input table validation: minimum 3 columns (date, value, at least 1 id)
    if (input.input_table_types.size() < 3) {
        throw InvalidInputException(
            "ts_combine_keys requires at least 3 columns: "
            "date_col, value_col, and at least one id_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Column layout: date_col, value_col, id_col1, id_col2, ...
    bind_data->date_col_name = input.input_table_names.size() > 0 ? input.input_table_names[0] : "date";
    bind_data->value_col_name = input.input_table_names.size() > 1 ? input.input_table_names[1] : "value";
    bind_data->num_id_cols = input.input_table_types.size() - 2;

    // Detect date column type
    bind_data->date_logical_type = input.input_table_types[0];
    switch (input.input_table_types[0].id()) {
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
            bind_data->date_col_type = DateColumnType::TIMESTAMP;
    }

    // Preserve value column type
    bind_data->value_logical_type = input.input_table_types[1];

    // Output schema: unique_id, date_col, value_col
    names.push_back("unique_id");
    return_types.push_back(LogicalType::VARCHAR);

    names.push_back(bind_data->date_col_name);
    return_types.push_back(bind_data->date_logical_type);

    names.push_back(bind_data->value_col_name);
    return_types.push_back(bind_data->value_logical_type);

    return bind_data;
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsCombineKeysInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {

    return make_uniq<TsCombineKeysGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsCombineKeysInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {

    return make_uniq<TsCombineKeysLocalState>();
}

// ============================================================================
// In-Out Function - direct pass-through with key combination
// ============================================================================

static OperatorResultType TsCombineKeysInOut(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data.bind_data->Cast<TsCombineKeysBindData>();

    output.Reset();
    idx_t output_idx = 0;

    for (idx_t row_idx = 0; row_idx < input.size(); row_idx++) {
        // Build combined unique_id
        string unique_id;
        for (idx_t i = 0; i < bind_data.num_id_cols; i++) {
            if (i > 0) {
                unique_id += bind_data.separator;
            }
            Value id_val = input.GetValue(2 + i, row_idx);
            unique_id += id_val.IsNull() ? "NULL" : id_val.ToString();
        }

        // Column 0: unique_id
        output.SetValue(0, output_idx, Value(unique_id));

        // Column 1: date (pass through)
        output.SetValue(1, output_idx, input.GetValue(0, row_idx));

        // Column 2: value (pass through)
        output.SetValue(2, output_idx, input.GetValue(1, row_idx));

        output_idx++;
    }

    output.SetCardinality(output_idx);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsCombineKeysFunction(ExtensionLoader &loader) {
    // Single function with optional MAP{} parameter
    TableFunction func("ts_combine_keys",
                       {LogicalType::TABLE},
                       nullptr,
                       TsCombineKeysBind,
                       TsCombineKeysInitGlobal,
                       TsCombineKeysInitLocal);

    // Named parameter for MAP{} - this allows optional params while keeping single overload
    func.named_parameters["params"] = LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR);

    // Set up as table-in-out function
    func.in_out_function = TsCombineKeysInOut;

    loader.RegisterFunction(func);
}

} // namespace duckdb
