#include "duckdb_compat.hpp"
#include "ts_validate_separator.hpp"
#include "ts_collect_finalize.hpp"  // TsCollectFinalizeBarrier (#285, 20-05)
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <set>
#include <vector>
#include <mutex>

namespace duckdb {

// ============================================================================
// ts_validate_separator - Native separator validation function
//
// Checks if a separator character exists in any ID column values.
// Supports arbitrary number of ID columns.
// Input table format: id_col1, id_col2, ...
// Output: separator, is_valid, n_conflicts, conflicting_values, message
//
// Named Parameters:
// - separator: Character(s) to validate (default: '|')
// ============================================================================

// ============================================================================
// Bind Data
// ============================================================================

struct TsValidateSeparatorBindData : public TableFunctionData {
    string separator = "|";
    idx_t num_id_cols = 0;
};

// ============================================================================
// Global State - holds all mutable data storage for thread safety
// ============================================================================

struct TsValidateSeparatorGlobalState : public GlobalTableFunctionState {
    // All data storage (protected by mutex)
    std::mutex groups_mutex;
    std::set<string> distinct_values;
    bool processed = false;
    bool output_done = false;

    // Finalize coordination (#285, 20-05: TsCollectFinalizeBarrier; a
    // single-pipeline input via (SELECT * FROM _ts_passthrough(source)
    // ORDER BY ALL), supplied by the public ts_validate_separator macro,
    // guarantees the barrier sees every executor that can hold rows).
    TsCollectFinalizeBarrier barrier;

    idx_t MaxThreads() const override {
        return 1;
    }
};

// ============================================================================
// Local State - minimal per-thread tracking
// ============================================================================

struct TsValidateSeparatorLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool arrived_at_final = false;
};

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsValidateSeparatorBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    TableBindNames &names) {

    auto bind_data = make_uniq<TsValidateSeparatorBindData>();

    // Parse named parameters. Skip NULL unconditionally (not just when
    // kNamedParamsHaveNullDefault): the ts_validate_separator macro
    // (ts_macros.cpp, #285/20-05) always passes `separator := separator`
    // explicitly, which is a literal default when the caller omitted it, on
    // every DuckDB line.
    for (auto &kv : input.named_parameters) {
        if (kv.second.IsNull()) {
            continue;
        }
        if (NameString(kv.first) == "separator") {
            bind_data->separator = kv.second.GetValue<string>();
        }
    }

    // Input table validation: at least 1 ID column
    if (input.input_table_types.empty()) {
        throw InvalidInputException(
            "ts_validate_separator requires at least 1 ID column.");
    }

    bind_data->num_id_cols = input.input_table_types.size();

    // Output schema
    names.push_back(MakeBindName("separator"));
    return_types.push_back(LogicalType::VARCHAR);

    names.push_back(MakeBindName("is_valid"));
    return_types.push_back(LogicalType::BOOLEAN);

    names.push_back(MakeBindName("n_conflicts"));
    return_types.push_back(LogicalType::INTEGER);

    names.push_back(MakeBindName("conflicting_values"));
    return_types.push_back(LogicalType::LIST(LogicalType::VARCHAR));

    names.push_back(MakeBindName("message"));
    return_types.push_back(LogicalType::VARCHAR);

    return bind_data;
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsValidateSeparatorInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {

    return make_uniq<TsValidateSeparatorGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsValidateSeparatorInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {

    global_state->Cast<TsValidateSeparatorGlobalState>().barrier.RegisterLocal();
    return make_uniq<TsValidateSeparatorLocalState>();
}

// ============================================================================
// In-Out Function - collects all distinct values
// ============================================================================

static OperatorResultType TsValidateSeparatorInOut(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &input,
    DataChunk &output) {

    auto &bind_data = data.bind_data->Cast<TsValidateSeparatorBindData>();
    auto &gstate = data.global_state->Cast<TsValidateSeparatorGlobalState>();

    gstate.barrier.AssertCollecting("_ts_validate_separator_native");

    // Extract batch locally first
    std::set<string> local_batch;

    // Collect all distinct values from all ID columns
    for (idx_t row_idx = 0; row_idx < input.size(); row_idx++) {
        for (idx_t col_idx = 0; col_idx < bind_data.num_id_cols; col_idx++) {
            Value val = input.GetValue(col_idx, row_idx);
            if (!val.IsNull()) {
                local_batch.insert(val.ToString());
            }
        }
    }

    // Insert into global state under lock
    {
        std::lock_guard<std::mutex> lock(gstate.groups_mutex);
        for (auto &val : local_batch) {
            gstate.distinct_values.insert(val);
        }
    }

    // Don't output anything during input phase
    SetOutputCardinality(output, 0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - outputs validation result
// ============================================================================

static OperatorFinalizeResultType TsValidateSeparatorFinalize(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &output) {

    auto &bind_data = data.bind_data->Cast<TsValidateSeparatorBindData>();
    auto &gstate = data.global_state->Cast<TsValidateSeparatorGlobalState>();
    auto &lstate = data.local_state->Cast<TsValidateSeparatorLocalState>();

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

    if (gstate.output_done) {
        return OperatorFinalizeResultType::FINISHED;
    }

    // Find conflicting values (values containing the separator)
    vector<Value> conflicting_values;
    for (const auto& val : gstate.distinct_values) {
        if (val.find(bind_data.separator) != string::npos) {
            conflicting_values.push_back(Value(val));
        }
    }

    idx_t n_conflicts = conflicting_values.size();
    bool is_valid = (n_conflicts == 0);

    // Build message
    string message;
    if (is_valid) {
        message = "Separator is safe to use";
    } else {
        message = "Separator '" + bind_data.separator + "' found in " +
                  std::to_string(n_conflicts) + " value(s). Try: ";

        vector<string> suggestions;
        if (bind_data.separator != "-" && bind_data.separator.find('-') == string::npos) {
            suggestions.push_back("'-'");
        }
        if (bind_data.separator != "." && bind_data.separator.find('.') == string::npos) {
            suggestions.push_back("'.'");
        }
        if (bind_data.separator != "::" && bind_data.separator.find("::") == string::npos) {
            suggestions.push_back("'::'");
        }
        if (bind_data.separator != "__" && bind_data.separator.find("__") == string::npos) {
            suggestions.push_back("'__'");
        }
        if (bind_data.separator != "#" && bind_data.separator.find('#') == string::npos) {
            suggestions.push_back("'#'");
        }

        for (size_t i = 0; i < suggestions.size(); i++) {
            if (i > 0) message += ", ";
            message += suggestions[i];
        }
    }

    // Output single row
    output.Reset();
    output.data[0].SetValue(0, Value(bind_data.separator));
    output.data[1].SetValue(0, Value::BOOLEAN(is_valid));
    output.data[2].SetValue(0, Value::INTEGER(static_cast<int32_t>(n_conflicts)));
    output.data[3].SetValue(0, Value::LIST(LogicalType::VARCHAR, conflicting_values));
    output.data[4].SetValue(0, Value(message));
    SetOutputCardinality(output, 1);

    gstate.output_done = true;
    return OperatorFinalizeResultType::FINISHED;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsValidateSeparatorFunction(ExtensionLoader &loader) {
    // Renamed native (#285, 20-05). The public name `ts_validate_separator` is
    // now a ts_table_macros[] entry (ts_macros.cpp) that feeds this function a
    // single-pipeline input via (SELECT * FROM _ts_passthrough(source) ORDER
    // BY ALL), so a UNION ALL source is read in full instead of a partial
    // distinct-value set. Signature and output columns are unchanged; the
    // description, example and category live on the macro entry.
    TableFunction func("_ts_validate_separator_native",
                       {LogicalType::TABLE},
                       nullptr,
                       TsValidateSeparatorBind,
                       TsValidateSeparatorInitGlobal,
                       TsValidateSeparatorInitLocal);

    // Named parameters
    AddNamedParameter(func, "separator", LogicalType::VARCHAR);

    // Set up as table-in-out function
    func.in_out_function = TsValidateSeparatorInOut;
    func.in_out_function_final = TsValidateSeparatorFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
