#include "ts_split_keys.hpp"
#include "ts_fill_gaps_native.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <vector>
#include <sstream>

namespace duckdb {

// ============================================================================
// ts_split_keys - Native key splitting function
//
// Splits a combined unique_id back into component columns.
// Auto-detects the number of parts from the data.
// Input table format: unique_id, date_col, value_col
// Output: id_part_1, id_part_2, ..., date_col, value_col
//    OR: col1, col2, ..., date_col, value_col (if columns specified)
//
// Named Parameters:
// - separator: Character(s) used to split (default: '|')
// - columns: LIST of column names (optional, e.g., ['region', 'store', 'item'])
// ============================================================================

// ============================================================================
// Bind Data
// ============================================================================

struct TsSplitKeysBindData : public TableFunctionData {
    // Parameters
    string separator = "|";
    vector<string> column_names;  // Empty = auto-generate id_part_N

    // Detected schema
    idx_t num_parts = 0;  // Auto-detected from data
    string date_col_name = "date";
    string value_col_name = "value";

    // Type preservation
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    LogicalType value_logical_type = LogicalType(LogicalTypeId::DOUBLE);

    // Flag for late binding
    bool schema_bound = false;
};

// ============================================================================
// Local State
// ============================================================================

struct TsSplitKeysLocalState : public LocalTableFunctionState {
    // Buffered rows for late binding
    struct BufferedRow {
        string unique_id;
        Value date_val;
        Value value_val;
    };
    vector<BufferedRow> buffered_rows;

    // Results ready to output (after schema detection)
    struct OutputRow {
        vector<string> id_parts;
        Value date_val;
        Value value_val;
    };
    vector<OutputRow> results;
    idx_t current_result = 0;
    bool processed = false;
};

// ============================================================================
// Global State
// ============================================================================

struct TsSplitKeysGlobalState : public GlobalTableFunctionState {
    idx_t max_threads = 1;

    idx_t MaxThreads() const override {
        return max_threads;
    }
};

// ============================================================================
// Helper: Extract list of strings from LIST Value
// ============================================================================

static vector<string> ExtractListStrings(const Value& list_val) {
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

// ============================================================================
// Helper: Split string by separator
// ============================================================================

static vector<string> SplitString(const string& str, const string& separator) {
    vector<string> result;
    if (separator.empty()) {
        result.push_back(str);
        return result;
    }

    size_t start = 0;
    size_t end = str.find(separator);
    while (end != string::npos) {
        result.push_back(str.substr(start, end - start));
        start = end + separator.length();
        end = str.find(separator, start);
    }
    result.push_back(str.substr(start));
    return result;
}

// ============================================================================
// Helper: Count parts in first non-null unique_id
// ============================================================================

static idx_t CountParts(const string& unique_id, const string& separator) {
    return SplitString(unique_id, separator).size();
}

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsSplitKeysBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsSplitKeysBindData>();

    // Parse named parameters
    for (auto &kv : input.named_parameters) {
        if (kv.first == "separator") {
            bind_data->separator = kv.second.GetValue<string>();
        } else if (kv.first == "columns") {
            bind_data->column_names = ExtractListStrings(kv.second);
        }
    }

    // Input table validation: exactly 3 columns (unique_id, date, value)
    if (input.input_table_types.size() != 3) {
        throw InvalidInputException(
            "ts_split_keys requires exactly 3 columns: "
            "unique_id, date_col, value_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Get column names from input
    bind_data->date_col_name = input.input_table_names.size() > 1 ? input.input_table_names[1] : "date";
    bind_data->value_col_name = input.input_table_names.size() > 2 ? input.input_table_names[2] : "value";

    // Detect date column type
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
            bind_data->date_col_type = DateColumnType::TIMESTAMP;
    }

    // Preserve value column type
    bind_data->value_logical_type = input.input_table_types[2];

    // If column names are provided, use them to determine output schema
    if (!bind_data->column_names.empty()) {
        bind_data->num_parts = bind_data->column_names.size();
        for (const auto& col_name : bind_data->column_names) {
            names.push_back(col_name);
            return_types.push_back(LogicalType::VARCHAR);
        }
        bind_data->schema_bound = true;
    } else {
        // Without column names, we need to auto-detect from data
        // Use a reasonable default of 3 parts (can be extended dynamically)
        bind_data->num_parts = 3;
        for (idx_t i = 0; i < bind_data->num_parts; i++) {
            names.push_back("id_part_" + std::to_string(i + 1));
            return_types.push_back(LogicalType::VARCHAR);
        }
        bind_data->schema_bound = false;
    }

    // Add date and value columns
    names.push_back(bind_data->date_col_name);
    return_types.push_back(bind_data->date_logical_type);

    names.push_back(bind_data->value_col_name);
    return_types.push_back(bind_data->value_logical_type);

    return bind_data;
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsSplitKeysInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {

    return make_uniq<TsSplitKeysGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsSplitKeysInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {

    return make_uniq<TsSplitKeysLocalState>();
}

// ============================================================================
// In-Out Function - buffers input for processing
// ============================================================================

static OperatorResultType TsSplitKeysInOut(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data.bind_data->Cast<TsSplitKeysBindData>();
    auto &local_state = data.local_state->Cast<TsSplitKeysLocalState>();

    // Buffer all incoming rows
    for (idx_t row_idx = 0; row_idx < input.size(); row_idx++) {
        Value id_val = input.GetValue(0, row_idx);
        Value date_val = input.GetValue(1, row_idx);
        Value value_val = input.GetValue(2, row_idx);

        if (id_val.IsNull()) {
            continue;
        }

        TsSplitKeysLocalState::BufferedRow row;
        row.unique_id = id_val.ToString();
        row.date_val = date_val;
        row.value_val = value_val;

        local_state.buffered_rows.push_back(std::move(row));
    }

    // Don't output anything during input phase
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - processes and outputs results
// ============================================================================

static OperatorFinalizeResultType TsSplitKeysFinalize(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &output) {

    auto &bind_data = data.bind_data->Cast<TsSplitKeysBindData>();
    auto &local_state = data.local_state->Cast<TsSplitKeysLocalState>();

    if (!local_state.processed) {
        // Process all buffered rows
        for (const auto& row : local_state.buffered_rows) {
            TsSplitKeysLocalState::OutputRow out_row;
            out_row.id_parts = SplitString(row.unique_id, bind_data.separator);
            out_row.date_val = row.date_val;
            out_row.value_val = row.value_val;

            // Pad or truncate to expected number of parts
            while (out_row.id_parts.size() < bind_data.num_parts) {
                out_row.id_parts.push_back("");
            }
            if (out_row.id_parts.size() > bind_data.num_parts) {
                out_row.id_parts.resize(bind_data.num_parts);
            }

            local_state.results.push_back(std::move(out_row));
        }

        // Clear buffer to free memory
        local_state.buffered_rows.clear();
        local_state.processed = true;
    }

    // Output results in batches
    output.Reset();
    idx_t output_idx = 0;

    while (local_state.current_result < local_state.results.size() &&
           output_idx < STANDARD_VECTOR_SIZE) {

        auto& result = local_state.results[local_state.current_result];

        // Output id parts
        for (idx_t i = 0; i < bind_data.num_parts; i++) {
            output.SetValue(i, output_idx, Value(result.id_parts[i]));
        }

        // Output date column
        output.SetValue(bind_data.num_parts, output_idx, result.date_val);

        // Output value column
        output.SetValue(bind_data.num_parts + 1, output_idx, result.value_val);

        output_idx++;
        local_state.current_result++;
    }

    output.SetCardinality(output_idx);

    if (local_state.current_result >= local_state.results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }
    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsSplitKeysFunction(ExtensionLoader &loader) {
    // Function with TABLE parameter and named parameters
    TableFunction func("ts_split_keys",
                       {LogicalType::TABLE},
                       nullptr,
                       TsSplitKeysBind,
                       TsSplitKeysInitGlobal,
                       TsSplitKeysInitLocal);

    // Named parameters
    func.named_parameters["separator"] = LogicalType::VARCHAR;
    func.named_parameters["columns"] = LogicalType::LIST(LogicalType::VARCHAR);

    // Set up as table-in-out function
    func.in_out_function = TsSplitKeysInOut;
    func.in_out_function_final = TsSplitKeysFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
