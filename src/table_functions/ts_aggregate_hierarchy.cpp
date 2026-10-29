#include "duckdb_compat.hpp"
#include "ts_aggregate_hierarchy.hpp"
#include "ts_collect_finalize.hpp"  // TsCollectFinalizeBarrier (#285, 20-05)
#include "ts_fill_gaps_native.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <cstring>
#include <numeric>
#include <unordered_map>
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
// Global State - holds all mutable data storage for thread safety
//
// Rows are stored column-wise; each row's id tuple is interned once as a leaf
// (#305), so Finalize builds every level's unique_id once per distinct leaf
// instead of once per row.
// ============================================================================

struct TsAggregateHierarchyGlobalState : public GlobalTableFunctionState {
    // All data storage (protected by mutex)
    std::mutex groups_mutex;
    vector<int64_t> row_dates;
    vector<double> row_values;
    vector<uint8_t> row_valid;
    vector<uint32_t> row_leaf;

    // Leaf interning: length-prefixed id tuple -> leaf index
    std::unordered_map<string, uint32_t> leaf_index;
    vector<vector<string>> leaf_ids;

    // Results: one cell per (unique_id, date), emitted in `result_order`
    struct Cell {
        uint32_t node;
        int64_t date_micros;
        double sum;
        bool any_valid;
    };
    vector<string> node_uids;
    vector<Cell> cells;
    vector<uint32_t> result_order;
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

    const idx_t count = input.size();
    const idx_t num_ids = bind_data.num_id_cols;

    // Read columns of the common types straight from their vectors; any other
    // type goes through Value with the same conversions as before (#305).
    UnifiedVectorFormat date_fmt, value_fmt;
    ToUnified(input.data[0], count, date_fmt);
    ToUnified(input.data[1], count, value_fmt);
    const auto date_type = input.data[0].GetType().id();
    const bool value_is_double = input.data[1].GetType().id() == LogicalTypeId::DOUBLE;

    vector<UnifiedVectorFormat> id_fmts(num_ids);
    vector<LogicalTypeId> id_types(num_ids);
    for (idx_t i = 0; i < num_ids; i++) {
        ToUnified(input.data[2 + i], count, id_fmts[i]);
        id_types[i] = input.data[2 + i].GetType().id();
    }

    auto read_id = [&](idx_t col, idx_t row_idx) -> string {
        auto &fmt = id_fmts[col];
        auto idx = fmt.sel->get_index(row_idx);
        if (!fmt.validity.RowIsValid(idx)) {
            return "NULL";
        }
        switch (id_types[col]) {
            case LogicalTypeId::VARCHAR:
                return UnifiedVectorFormat::GetData<string_t>(fmt)[idx].GetString();
            case LogicalTypeId::INTEGER:
                return std::to_string(UnifiedVectorFormat::GetData<int32_t>(fmt)[idx]);
            case LogicalTypeId::BIGINT:
                return std::to_string(UnifiedVectorFormat::GetData<int64_t>(fmt)[idx]);
            default:
                return input.GetValue(2 + col, row_idx).ToString();
        }
    };

    std::lock_guard<std::mutex> lock(gstate.groups_mutex);
    vector<string> ids(num_ids);
    string key;
    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        auto date_idx = date_fmt.sel->get_index(row_idx);
        if (!date_fmt.validity.RowIsValid(date_idx)) {
            continue;
        }

        // Convert date to microseconds
        int64_t date_micros;
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                date_micros = DateToMicroseconds(UnifiedVectorFormat::GetData<date_t>(date_fmt)[date_idx]);
                break;
            case DateColumnType::TIMESTAMP:
                if (date_type == LogicalTypeId::TIMESTAMP || date_type == LogicalTypeId::TIMESTAMP_TZ) {
                    date_micros = TimestampToMicroseconds(UnifiedVectorFormat::GetData<timestamp_t>(date_fmt)[date_idx]);
                } else {
                    date_micros = TimestampToMicroseconds(input.GetValue(0, row_idx).GetValue<timestamp_t>());
                }
                break;
            case DateColumnType::INTEGER:
                date_micros = static_cast<int64_t>(UnifiedVectorFormat::GetData<int32_t>(date_fmt)[date_idx]);
                break;
            case DateColumnType::BIGINT:
                date_micros = UnifiedVectorFormat::GetData<int64_t>(date_fmt)[date_idx];
                break;
        }

        auto value_idx = value_fmt.sel->get_index(row_idx);
        bool valid = value_fmt.validity.RowIsValid(value_idx);
        double value = 0.0;
        if (valid) {
            value = value_is_double ? UnifiedVectorFormat::GetData<double>(value_fmt)[value_idx]
                                    : input.GetValue(1, row_idx).GetValue<double>();
        }

        // Intern the id tuple (length-prefixed, so distinct tuples never collide)
        key.clear();
        for (idx_t i = 0; i < num_ids; i++) {
            ids[i] = read_id(i, row_idx);
            uint32_t len = static_cast<uint32_t>(ids[i].size());
            key.append(reinterpret_cast<const char *>(&len), sizeof(len));
            key.append(ids[i]);
        }
        auto it = gstate.leaf_index.find(key);
        uint32_t leaf;
        if (it == gstate.leaf_index.end()) {
            leaf = static_cast<uint32_t>(gstate.leaf_ids.size());
            gstate.leaf_index.emplace(key, leaf);
            gstate.leaf_ids.push_back(ids);
        } else {
            leaf = it->second;
        }

        gstate.row_dates.push_back(date_micros);
        gstate.row_values.push_back(value);
        gstate.row_valid.push_back(valid);
        gstate.row_leaf.push_back(leaf);
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
        const idx_t levels = bind_data.num_id_cols + 1;
        const idx_t num_leaves = gstate.leaf_ids.size();

        // Hierarchy nodes: one per distinct unique_id over all levels (0 to
        // num_id_cols), built once per leaf. Keyed by the unique_id string, so
        // tuples whose ids join to the same string share a node, as before.
        std::unordered_map<string, uint32_t> node_index;
        vector<uint32_t> leaf_nodes(num_leaves * levels);
        for (idx_t leaf = 0; leaf < num_leaves; leaf++) {
            for (idx_t level = 0; level < levels; level++) {
                string unique_id = BuildUniqueId(
                    gstate.leaf_ids[leaf],
                    level,
                    bind_data.separator,
                    bind_data.aggregate_keyword,
                    bind_data.num_id_cols
                );
                auto inserted = node_index.emplace(std::move(unique_id), static_cast<uint32_t>(gstate.node_uids.size()));
                if (inserted.second) {
                    gstate.node_uids.push_back(inserted.first->first);
                }
                leaf_nodes[leaf * levels + level] = inserted.first->second;
            }
        }
        const idx_t num_nodes = gstate.node_uids.size();

        // Visit rows by date; the stable sort keeps input order within a date,
        // so each (unique_id, date) cell sums its values in input order.
        const idx_t num_rows = gstate.row_dates.size();
        const auto &dates = gstate.row_dates;
        vector<idx_t> order(num_rows);
        std::iota(order.begin(), order.end(), 0);
        if (!std::is_sorted(dates.begin(), dates.end())) {
            std::stable_sort(order.begin(), order.end(),
                [&](idx_t a, idx_t b) { return dates[a] < dates[b]; });
        }

        // One cell per (node, date): within a date block a node's cell is found
        // through `node_cell`, valid while `node_block` equals the current block.
        vector<int64_t> node_block(num_nodes, -1);
        vector<uint32_t> node_cell(num_nodes, 0);
        vector<uint32_t> node_cell_count(num_nodes, 0);
        int64_t block = -1;
        for (idx_t k = 0; k < num_rows; k++) {
            idx_t row = order[k];
            if (k == 0 || dates[row] != dates[order[k - 1]]) {
                block++;
            }
            const uint32_t *nodes = &leaf_nodes[gstate.row_leaf[row] * levels];
            for (idx_t level = 0; level < levels; level++) {
                uint32_t node = nodes[level];
                if (node_block[node] != block) {
                    node_block[node] = block;
                    node_cell[node] = static_cast<uint32_t>(gstate.cells.size());
                    node_cell_count[node]++;
                    gstate.cells.push_back({node, dates[row], 0.0, false});
                }
                auto &cell = gstate.cells[node_cell[node]];
                if (gstate.row_valid[row]) {
                    cell.sum += gstate.row_values[row];
                    cell.any_valid = true;
                }
            }
        }

        // Emit by unique_id, then date: cells of one node were created in date
        // order, so a counting sort over the nodes' string order suffices.
        vector<uint32_t> nodes_sorted(num_nodes);
        std::iota(nodes_sorted.begin(), nodes_sorted.end(), 0);
        std::sort(nodes_sorted.begin(), nodes_sorted.end(),
            [&](uint32_t a, uint32_t b) { return gstate.node_uids[a] < gstate.node_uids[b]; });
        vector<idx_t> node_start(num_nodes);
        idx_t offset = 0;
        for (auto node : nodes_sorted) {
            node_start[node] = offset;
            offset += node_cell_count[node];
        }
        gstate.result_order.resize(gstate.cells.size());
        for (idx_t c = 0; c < gstate.cells.size(); c++) {
            gstate.result_order[node_start[gstate.cells[c].node]++] = static_cast<uint32_t>(c);
        }

        // Clear input buffers to free memory
        gstate.row_dates = vector<int64_t>();
        gstate.row_values = vector<double>();
        gstate.row_valid = vector<uint8_t>();
        gstate.row_leaf = vector<uint32_t>();
        gstate.leaf_index.clear();
        gstate.leaf_ids = vector<vector<string>>();
        gstate.processed = true;
    }

    // Output results in batches
    output.Reset();
    idx_t output_idx = 0;

    auto &uid_vec = output.data[0];
    auto &date_vec = output.data[1];
    auto &value_vec = output.data[2];
    auto uid_data = FlatDataMut<string_t>(uid_vec);
    auto value_data = FlatDataMut<double>(value_vec);
    const auto date_type = bind_data.date_logical_type.id();

    while (gstate.current_result < gstate.result_order.size() &&
           output_idx < STANDARD_VECTOR_SIZE) {

        auto &cell = gstate.cells[gstate.result_order[gstate.current_result]];

        // Column 0: unique_id
        uid_data[output_idx] = StringVector::AddString(uid_vec, gstate.node_uids[cell.node]);

        // Column 1: date (preserve original type)
        switch (bind_data.date_col_type) {
            case DateColumnType::DATE:
                FlatDataMut<date_t>(date_vec)[output_idx] = MicrosecondsToDate(cell.date_micros);
                break;
            case DateColumnType::TIMESTAMP:
                if (date_type == LogicalTypeId::TIMESTAMP || date_type == LogicalTypeId::TIMESTAMP_TZ) {
                    FlatDataMut<timestamp_t>(date_vec)[output_idx] = MicrosecondsToTimestamp(cell.date_micros);
                } else {
                    date_vec.SetValue(output_idx, Value::TIMESTAMP(MicrosecondsToTimestamp(cell.date_micros)));
                }
                break;
            case DateColumnType::INTEGER:
                FlatDataMut<int32_t>(date_vec)[output_idx] = static_cast<int32_t>(cell.date_micros);
                break;
            case DateColumnType::BIGINT:
                FlatDataMut<int64_t>(date_vec)[output_idx] = cell.date_micros;
                break;
        }

        // Column 2: value — SQL SUM semantics: NULL when no leaf value was present,
        // otherwise the sum of the present values (no NULL->0 imputation).
        if (cell.any_valid) {
            value_data[output_idx] = cell.sum;
        } else {
            FlatVector::SetNull(value_vec, output_idx, true);
        }

        output_idx++;
        gstate.current_result++;
    }

    SetOutputCardinality(output, output_idx);

    if (gstate.current_result >= gstate.result_order.size()) {
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
