#include "duckdb_compat.hpp"
#include "ts_aggregate_hierarchy.hpp"
#include "ts_collect_finalize.hpp"  // TsCollectFinalizeBarrier (#285, 20-05)
#include "ts_fill_gaps_native.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <vector>
#include <mutex>

namespace duckdb {

// ============================================================================
// ts_aggregate_hierarchy - Native hierarchical aggregation function
//
// Supports arbitrary hierarchy levels (2-N ID columns).
// Input table format: date_col, value_col, id_col1, id_col2, ...
// Output: unique_id, date_col, value_col
//
// For N ID columns, generates N+1 aggregation levels per unique date:
// - Level 0: All IDs = AGGREGATED (grand total)
// - Level 1: First ID kept, rest = AGGREGATED
// - Level N: All IDs kept (original data)
//
// Parameters via MAP{}:
// - separator: Character(s) to join ID parts (default: '|')
// - aggregate_keyword: Keyword for aggregated levels (default: 'AGGREGATED')
// ============================================================================

// ============================================================================
// Bind Data
// ============================================================================

struct TsAggregateHierarchyBindData : public TableFunctionData {
    // Parameters
    string separator = "|";
    string aggregate_keyword = "AGGREGATED";

    // Input schema info
    idx_t num_id_cols = 0;
    vector<string> id_col_names;
    string date_col_name = "date";
    string value_col_name = "value";

    // Type preservation
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
};

// ============================================================================
// Standalone data structs (moved out of LocalState for parallel safety)
// ============================================================================

struct TsAggHierRowData {
    int64_t date_micros;
    double value;
    bool valid;
    vector<string> id_values;
};

struct TsAggHierOutputRow {
    string unique_id;
    int64_t date_micros;
    double value;
    bool any_valid;
};

// ============================================================================
// Global State - holds all mutable data storage for thread safety
// ============================================================================

struct TsAggregateHierarchyGlobalState : public GlobalTableFunctionState {
    // All data storage (protected by mutex)
    std::mutex groups_mutex;
    vector<TsAggHierRowData> rows;
    vector<TsAggHierOutputRow> results;
    idx_t current_result = 0;
    bool processed = false;

    // Finalize coordination (#285, 20-05: TsCollectFinalizeBarrier; a
    // single-pipeline input via (SELECT * FROM _ts_passthrough(source)
    // ORDER BY ALL), supplied by the public ts_aggregate_hierarchy macro,
    // guarantees the barrier sees every executor that can hold rows).
    TsCollectFinalizeBarrier barrier;

    idx_t MaxThreads() const override {
        return 1;
    }
};

// ============================================================================
// Local State - minimal per-thread tracking
// ============================================================================

struct TsAggregateHierarchyLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool arrived_at_final = false;
};

// ============================================================================
// Helper: Build unique_id for a given hierarchy level
// ============================================================================

// level=0: AGGREGATED|AGGREGATED|AGGREGATED (grand total)
// level=1: id1|AGGREGATED|AGGREGATED
// level=2: id1|id2|AGGREGATED
// level=N: id1|id2|id3 (original)
static string BuildUniqueId(
    const vector<string>& id_values,
    idx_t level,
    const string& separator,
    const string& aggregate_keyword,
    idx_t num_id_cols) {

    string result;
    for (idx_t i = 0; i < num_id_cols; i++) {
        if (i > 0) {
            result += separator;
        }

        if (i < level) {
            // Keep original ID value
            result += id_values[i];
        } else {
            // Replace with aggregate keyword
            result += aggregate_keyword;
        }
    }
    return result;
}

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

static unique_ptr<FunctionData> TsAggregateHierarchyBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    TableBindNames &names) {

    auto bind_data = make_uniq<TsAggregateHierarchyBindData>();

    // Parse MAP{} parameters from positional argument (index 1, after TABLE at index 0)
    if (input.inputs.size() > 1 && !input.inputs[1].IsNull()) {
        auto& map_val = input.inputs[1];
        bind_data->separator = ExtractMapString(map_val, "separator", "|");
        bind_data->aggregate_keyword = ExtractMapString(map_val, "aggregate_keyword", "AGGREGATED");
    }

    // Input table validation: minimum 3 columns (date, value, at least 1 id)
    if (input.input_table_types.size() < 3) {
        throw InvalidInputException(
            "ts_aggregate_hierarchy requires at least 3 columns: "
            "date_col, value_col, and at least one id_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Column layout: date_col, value_col, id_col1, id_col2, ...
    bind_data->date_col_name = input.input_table_names.size() > 0 ? NameString(input.input_table_names[0]) : "date";
    bind_data->value_col_name = input.input_table_names.size() > 1 ? NameString(input.input_table_names[1]) : "value";
    bind_data->num_id_cols = input.input_table_types.size() - 2;

    for (idx_t i = 2; i < input.input_table_types.size(); i++) {
        bind_data->id_col_names.push_back(
            i < input.input_table_names.size() ? NameString(input.input_table_names[i]) : "id_" + std::to_string(i - 1)
        );
    }

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

    // Output schema: unique_id, date_col, value_col
    names.push_back(MakeBindName("unique_id"));
    return_types.push_back(LogicalType::VARCHAR);

    names.push_back(MakeBindName(bind_data->date_col_name));
    return_types.push_back(bind_data->date_logical_type);

    names.push_back(MakeBindName(bind_data->value_col_name));
    return_types.push_back(LogicalType::DOUBLE);

    return bind_data;
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsAggregateHierarchyInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {

    return make_uniq<TsAggregateHierarchyGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsAggregateHierarchyInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {

    global_state->Cast<TsAggregateHierarchyGlobalState>().barrier.RegisterLocal();
    return make_uniq<TsAggregateHierarchyLocalState>();
}

// ============================================================================
// In-Out Function - buffers all input rows
// ============================================================================

static OperatorResultType TsAggregateHierarchyInOut(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data.bind_data->Cast<TsAggregateHierarchyBindData>();
    auto &gstate = data.global_state->Cast<TsAggregateHierarchyGlobalState>();

    gstate.barrier.AssertCollecting("_ts_aggregate_hierarchy_native");

    // Extract batch locally first
    vector<TsAggHierRowData> local_batch;

    // Buffer all incoming rows
    for (idx_t row_idx = 0; row_idx < input.size(); row_idx++) {
        Value date_val = input.GetValue(0, row_idx);
        Value value_val = input.GetValue(1, row_idx);

        if (date_val.IsNull()) {
            continue;
        }

        TsAggHierRowData row;

        // Convert date to microseconds
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                row.date_micros = DateToMicroseconds(date_val.GetValue<date_t>());
                break;
            case DateColumnType::TIMESTAMP:
                row.date_micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
                break;
            case DateColumnType::INTEGER:
                row.date_micros = static_cast<int64_t>(date_val.GetValue<int32_t>());
                break;
            case DateColumnType::BIGINT:
                row.date_micros = date_val.GetValue<int64_t>();
                break;
        }

        row.valid = !value_val.IsNull();
        row.value = row.valid ? value_val.GetValue<double>() : 0.0;

        // Extract all ID column values
        for (idx_t i = 0; i < bind_data.num_id_cols; i++) {
            Value id_val = input.GetValue(2 + i, row_idx);
            row.id_values.push_back(id_val.IsNull() ? "NULL" : id_val.ToString());
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

    // Don't output anything during input phase
    SetOutputCardinality(output, 0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - aggregates and outputs results
// ============================================================================

static OperatorFinalizeResultType TsAggregateHierarchyFinalize(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &output) {

    auto &bind_data = data.bind_data->Cast<TsAggregateHierarchyBindData>();
    auto &gstate = data.global_state->Cast<TsAggregateHierarchyGlobalState>();
    auto &lstate = data.local_state->Cast<TsAggregateHierarchyLocalState>();

    // Last-finalizer claim (ts_collect_finalize.hpp): the local state whose
    // arrival completes the count of registered local states processes; every
    // other one finishes at once (no spin-wait).
    if (!lstate.owns_finalize) {
        if (lstate.arrived_at_final) {
            return OperatorFinalizeResultType::FINISHED;
        }
        lstate.arrived_at_final = true;
        if (!gstate.barrier.ArriveAtFinal()) {
            return OperatorFinalizeResultType::FINISHED;
        }
        lstate.owns_finalize = true;
    }

    if (!gstate.processed) {
        // Build aggregation for each hierarchy level
        // Key: unique_id -> (date -> {sum of present values, any_valid})
        struct AggCell {
            double sum = 0.0;
            bool any_valid = false;
        };
        std::map<string, std::map<int64_t, AggCell>> aggregations;

        for (const auto& row : gstate.rows) {
            // Generate unique_id for each hierarchy level (0 to num_id_cols)
            for (idx_t level = 0; level <= bind_data.num_id_cols; level++) {
                string unique_id = BuildUniqueId(
                    row.id_values,
                    level,
                    bind_data.separator,
                    bind_data.aggregate_keyword,
                    bind_data.num_id_cols
                );

                auto &cell = aggregations[unique_id][row.date_micros];
                if (row.valid) {
                    cell.sum += row.value;
                    cell.any_valid = true;
                }
            }
        }

        // Flatten to output rows
        for (const auto& agg_entry : aggregations) {
            const string& unique_id = agg_entry.first;
            for (const auto& date_entry : agg_entry.second) {
                TsAggHierOutputRow out_row;
                out_row.unique_id = unique_id;
                out_row.date_micros = date_entry.first;
                out_row.value = date_entry.second.sum;
                out_row.any_valid = date_entry.second.any_valid;
                gstate.results.push_back(std::move(out_row));
            }
        }

        // Sort by unique_id, then date
        std::sort(gstate.results.begin(), gstate.results.end(),
            [](const TsAggHierOutputRow& a,
               const TsAggHierOutputRow& b) {
                if (a.unique_id != b.unique_id) return a.unique_id < b.unique_id;
                return a.date_micros < b.date_micros;
            });

        // Clear input buffer to free memory
        gstate.rows.clear();
        gstate.processed = true;
    }

    // Output results in batches
    output.Reset();
    idx_t output_idx = 0;

    while (gstate.current_result < gstate.results.size() &&
           output_idx < STANDARD_VECTOR_SIZE) {

        auto& result = gstate.results[gstate.current_result];

        // Column 0: unique_id
        output.data[0].SetValue(output_idx, Value(result.unique_id));

        // Column 1: date (preserve original type)
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                output.data[1].SetValue(output_idx, Value::DATE(MicrosecondsToDate(result.date_micros)));
                break;
            case DateColumnType::TIMESTAMP:
                output.data[1].SetValue(output_idx, Value::TIMESTAMP(MicrosecondsToTimestamp(result.date_micros)));
                break;
            case DateColumnType::INTEGER:
                output.data[1].SetValue(output_idx, Value::INTEGER(static_cast<int32_t>(result.date_micros)));
                break;
            case DateColumnType::BIGINT:
                output.data[1].SetValue(output_idx, Value::BIGINT(result.date_micros));
                break;
        }

        // Column 2: value — SQL SUM semantics: NULL when no leaf value was present,
        // otherwise the sum of the present values (no NULL->0 imputation).
        output.data[2].SetValue(output_idx,
            result.any_valid ? Value::DOUBLE(result.value) : Value(LogicalType::DOUBLE));

        output_idx++;
        gstate.current_result++;
    }

    SetOutputCardinality(output, output_idx);

    if (gstate.current_result >= gstate.results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }
    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsAggregateHierarchyFunction(ExtensionLoader &loader) {
    // Renamed native (#285, 20-05). The public name `ts_aggregate_hierarchy` is
    // now a ts_table_macros[] entry (ts_macros.cpp) that feeds this function a
    // single-pipeline input via (SELECT * FROM _ts_passthrough(source) ORDER BY
    // ALL), so a UNION ALL source is read in full instead of silently
    // truncated. Signature and output columns are unchanged; the description,
    // example and category live on the macro entry.
    TableFunction func("_ts_aggregate_hierarchy_native",
                       {LogicalType::TABLE, LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)},
                       nullptr,
                       TsAggregateHierarchyBind,
                       TsAggregateHierarchyInitGlobal,
                       TsAggregateHierarchyInitLocal);

    // Set up as table-in-out function
    func.in_out_function = TsAggregateHierarchyInOut;
    func.in_out_function_final = TsAggregateHierarchyFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
