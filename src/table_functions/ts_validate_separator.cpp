#include "ts_validate_separator.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <set>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>

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

    // Finalize coordination
    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};

    idx_t MaxThreads() const override {
        return 1;
    }
};

// ============================================================================
// Local State - minimal per-thread tracking
// ============================================================================

struct TsValidateSeparatorLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool registered_collector = false;
    bool registered_finalizer = false;
};

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsValidateSeparatorBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsValidateSeparatorBindData>();

    // Parse named parameters
    for (auto &kv : input.named_parameters) {
        if (kv.first == "separator") {
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
    names.push_back("separator");
    return_types.push_back(LogicalType::VARCHAR);

    names.push_back("is_valid");
    return_types.push_back(LogicalType::BOOLEAN);

    names.push_back("n_conflicts");
    return_types.push_back(LogicalType::INTEGER);

    names.push_back("conflicting_values");
    return_types.push_back(LogicalType::LIST(LogicalType::VARCHAR));

    names.push_back("message");
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
    auto &lstate = data.local_state->Cast<TsValidateSeparatorLocalState>();

    if (!lstate.registered_collector) {
        gstate.threads_collecting.fetch_add(1);
        lstate.registered_collector = true;
    }

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
    output.SetCardinality(0);
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
    output.SetValue(0, 0, Value(bind_data.separator));
    output.SetValue(1, 0, Value::BOOLEAN(is_valid));
    output.SetValue(2, 0, Value::INTEGER(static_cast<int32_t>(n_conflicts)));
    output.SetValue(3, 0, Value::LIST(LogicalType::VARCHAR, conflicting_values));
    output.SetValue(4, 0, Value(message));
    output.SetCardinality(1);

    gstate.output_done = true;
    return OperatorFinalizeResultType::FINISHED;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsValidateSeparatorFunction(ExtensionLoader &loader) {
    // Function with TABLE parameter and named parameters
    TableFunction func("ts_validate_separator",
                       {LogicalType::TABLE},
                       nullptr,
                       TsValidateSeparatorBind,
                       TsValidateSeparatorInitGlobal,
                       TsValidateSeparatorInitLocal);

    // Named parameters
    func.named_parameters["separator"] = LogicalType::VARCHAR;

    // Set up as table-in-out function
    func.in_out_function = TsValidateSeparatorInOut;
    func.in_out_function_final = TsValidateSeparatorFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
