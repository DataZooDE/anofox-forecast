#include "ts_mstl_decomposition_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For GetGroupKey, etc.
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <cstring>
#include <mutex>
#include <atomic>
#include <thread>

namespace duckdb {

// ============================================================================
// _ts_mstl_decomposition_native - Internal native streaming MSTL decomposition
//
// This is an INTERNAL function used by ts_mstl_decomposition_by macro.
// Users should call ts_mstl_decomposition_by() instead of this function directly.
//
// MEMORY FOOTPRINT:
//   - Native (this function): O(group_size) per group
//   - Old SQL macro approach: O(total_rows) due to LIST() aggregation
//
// Input columns: group_col, date_col, value_col
// Groups by group_col and generates decomposition for each group.
// ============================================================================

// ============================================================================
// Bind Data - captures all parameters
// ============================================================================

struct TsMstlDecompositionNativeBindData : public TableFunctionData {
    // Parameters
    int insufficient_data_mode = 0;  // 0=fail, 1=skip, 2=pad

    // Type preservation
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
};

// ============================================================================
// Group Data and Result Structures
// ============================================================================

struct MstlGroupData {
    Value group_value;
    vector<int64_t> dates;  // microseconds for sorting
    vector<double> values;
};

struct DecompositionOutputRow {
    Value group_value;
    vector<double> trend;
    vector<vector<double>> seasonal;  // one array per period
    vector<double> remainder;
    vector<int32_t> periods;
};

// ============================================================================
// Local State - per-thread flags only
// ============================================================================

struct TsMstlDecompositionNativeLocalState : public LocalTableFunctionState {
    bool owns_finalize = false;
    bool registered_collector = false;
    bool registered_finalizer = false;
};

// ============================================================================
// Helper Functions
// ============================================================================

static int ParseInsufficientDataMode(const string &mode) {
    if (mode == "fail") return 0;
    if (mode == "skip") return 1;
    if (mode == "pad") return 2;
    return 0;  // default: fail
}

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsMstlDecompositionNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsMstlDecompositionNativeBindData>();

    // Parse insufficient_data mode (parameter index 1)
    if (input.inputs.size() >= 2 && !input.inputs[1].IsNull()) {
        string mode = input.inputs[1].GetValue<string>();
        bind_data->insufficient_data_mode = ParseInsufficientDataMode(mode);
    }

    // Detect column types from input
    // Input table: group_col, date_col, value_col
    bind_data->group_logical_type = input.input_table_types[0];

    // Output schema: <group_col>, trend[], seasonal[][], remainder[], periods[]
    string group_col_name = input.input_table_names.size() > 0 ? input.input_table_names[0] : "id";
    names.push_back(group_col_name);
    return_types.push_back(bind_data->group_logical_type);

    names.push_back("trend");
    return_types.push_back(LogicalType::LIST(LogicalType::DOUBLE));

    names.push_back("seasonal");
    return_types.push_back(LogicalType::LIST(LogicalType::LIST(LogicalType::DOUBLE)));

    names.push_back("remainder");
    return_types.push_back(LogicalType::LIST(LogicalType::DOUBLE));

    names.push_back("periods");
    return_types.push_back(LogicalType::LIST(LogicalType::INTEGER));

    return bind_data;
}

// ============================================================================
// Global State - thread-safe group collection + single-thread finalize
// ============================================================================

struct TsMstlDecompositionNativeGlobalState : public GlobalTableFunctionState {
    idx_t MaxThreads() const override {
        return 999999;
    }

    // Thread-safe group storage (moved from LocalState)
    std::mutex groups_mutex;
    std::map<string, MstlGroupData> groups;
    vector<string> group_order;

    // Processing results (used by finalize owner)
    vector<DecompositionOutputRow> results;
    bool processed = false;
    idx_t output_offset = 0;

    // Single-thread finalize + barrier
    std::atomic<bool> finalize_claimed{false};
    std::atomic<idx_t> threads_collecting{0};
    std::atomic<idx_t> threads_done_collecting{0};
};

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsMstlDecompositionNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<TsMstlDecompositionNativeGlobalState>();
}

static unique_ptr<LocalTableFunctionState> TsMstlDecompositionNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {
    return make_uniq<TsMstlDecompositionNativeLocalState>();
}

// ============================================================================
// In-Out Function - buffers incoming data
// ============================================================================

static OperatorResultType TsMstlDecompositionNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &input,
    DataChunk &output) {

    auto &gstate = data_p.global_state->Cast<TsMstlDecompositionNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsMstlDecompositionNativeLocalState>();

    // Register this thread as a collector (first call only)
    if (!lstate.registered_collector) {
        gstate.threads_collecting.fetch_add(1);
        lstate.registered_collector = true;
    }

    // Extract batch locally (no lock)
    struct TempRow {
        Value group_val;
        string group_key;
        int64_t date_micros;
        double value;
    };
    vector<TempRow> batch;

    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull()) continue;

        TempRow row;
        row.group_val = group_val;
        row.group_key = GetGroupKey(group_val);

        // Get date as microseconds for sorting
        row.date_micros = 0;
        if (date_val.type().id() == LogicalTypeId::TIMESTAMP) {
            row.date_micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
        } else if (date_val.type().id() == LogicalTypeId::DATE) {
            row.date_micros = DateToMicroseconds(date_val.GetValue<date_t>());
        } else if (date_val.type().id() == LogicalTypeId::BIGINT) {
            row.date_micros = date_val.GetValue<int64_t>();
        } else if (date_val.type().id() == LogicalTypeId::INTEGER) {
            row.date_micros = date_val.GetValue<int32_t>();
        }

        row.value = value_val.IsNull() ? 0.0 : value_val.GetValue<double>();
        batch.push_back(std::move(row));
    }

    // Lock once, insert all
    {
        std::lock_guard<std::mutex> lock(gstate.groups_mutex);
        for (auto &row : batch) {
            if (gstate.groups.find(row.group_key) == gstate.groups.end()) {
                gstate.groups[row.group_key] = MstlGroupData();
                gstate.groups[row.group_key].group_value = row.group_val;
                gstate.group_order.push_back(row.group_key);
            }
            auto &grp = gstate.groups[row.group_key];
            grp.dates.push_back(row.date_micros);
            grp.values.push_back(row.value);
        }
    }

    // Don't output anything during input phase
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - process accumulated data and output results
// ============================================================================

static OperatorFinalizeResultType TsMstlDecompositionNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data_p,
    DataChunk &output) {

    auto &bind_data = data_p.bind_data->Cast<TsMstlDecompositionNativeBindData>();
    auto &gstate = data_p.global_state->Cast<TsMstlDecompositionNativeGlobalState>();
    auto &lstate = data_p.local_state->Cast<TsMstlDecompositionNativeLocalState>();

    // Barrier + claim
    if (!lstate.registered_finalizer) {
        if (lstate.registered_collector) {
            gstate.threads_done_collecting.fetch_add(1);
        }
        lstate.registered_finalizer = true;
    }
    if (!lstate.owns_finalize) {
        bool expected = false;
        if (!gstate.finalize_claimed.compare_exchange_strong(expected, true)) {
            return OperatorFinalizeResultType::FINISHED;
        }
        lstate.owns_finalize = true;
        while (gstate.threads_done_collecting.load() < gstate.threads_collecting.load()) {
            std::this_thread::yield();
        }
    }

    // Process all groups (single thread)
    if (!gstate.processed) {
        for (const auto &group_key : gstate.group_order) {
            auto &grp = gstate.groups[group_key];

            if (grp.dates.empty()) continue;

            // Sort by date
            vector<size_t> indices(grp.dates.size());
            for (size_t i = 0; i < indices.size(); i++) indices[i] = i;
            std::sort(indices.begin(), indices.end(),
                [&grp](size_t a, size_t b) { return grp.dates[a] < grp.dates[b]; });

            vector<double> sorted_values(grp.values.size());
            for (size_t i = 0; i < indices.size(); i++) {
                sorted_values[i] = grp.values[indices[i]];
            }

            // Call Rust FFI
            MstlResult mstl_result;
            memset(&mstl_result, 0, sizeof(mstl_result));
            AnofoxError error;

            bool success = anofox_ts_mstl_decomposition(
                sorted_values.data(),
                sorted_values.size(),
                nullptr,  // periods - auto detect
                0,
                bind_data.insufficient_data_mode,
                &mstl_result,
                &error
            );

            DecompositionOutputRow row;
            row.group_value = grp.group_value;

            if (success && mstl_result.decomposition_applied) {
                // Copy trend
                if (mstl_result.trend != nullptr) {
                    for (size_t i = 0; i < mstl_result.n_observations; i++) {
                        row.trend.push_back(mstl_result.trend[i]);
                    }
                }

                // Copy seasonal components
                if (mstl_result.seasonal_components != nullptr) {
                    for (size_t p = 0; p < mstl_result.n_seasonal; p++) {
                        vector<double> seasonal_p;
                        if (mstl_result.seasonal_components[p] != nullptr) {
                            for (size_t i = 0; i < mstl_result.n_observations; i++) {
                                seasonal_p.push_back(mstl_result.seasonal_components[p][i]);
                            }
                        }
                        row.seasonal.push_back(seasonal_p);
                    }
                }

                // Copy remainder
                if (mstl_result.remainder != nullptr) {
                    for (size_t i = 0; i < mstl_result.n_observations; i++) {
                        row.remainder.push_back(mstl_result.remainder[i]);
                    }
                }

                // Copy periods
                if (mstl_result.seasonal_periods != nullptr) {
                    for (size_t i = 0; i < mstl_result.n_seasonal; i++) {
                        row.periods.push_back(mstl_result.seasonal_periods[i]);
                    }
                }

                // Free Rust-allocated memory
                anofox_free_mstl_result(&mstl_result);
            } else if (bind_data.insufficient_data_mode == 1) {
                // skip mode - output empty arrays
                // (row already has empty vectors)
            } else {
                // fail mode or pad mode with failure - skip this group
                continue;
            }

            gstate.results.push_back(row);
        }

        gstate.processed = true;
    }

    // Output results in batches
    idx_t remaining = gstate.results.size() - gstate.output_offset;
    if (remaining == 0) {
        output.SetCardinality(0);
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t to_output = std::min(remaining, static_cast<idx_t>(STANDARD_VECTOR_SIZE));
    output.SetCardinality(to_output);

    // Initialize all output vectors as FLAT_VECTOR for parallel-safe batch merging
    for (idx_t col = 0; col < output.ColumnCount(); col++) {
        output.data[col].SetVectorType(VectorType::FLAT_VECTOR);
    }

    for (idx_t i = 0; i < to_output; i++) {
        auto &row = gstate.results[gstate.output_offset + i];

        // id
        output.data[0].SetValue(i, row.group_value);

        // trend[]
        vector<Value> trend_values;
        for (auto &v : row.trend) {
            trend_values.push_back(Value::DOUBLE(v));
        }
        output.data[1].SetValue(i, Value::LIST(LogicalType::DOUBLE, trend_values));

        // seasonal[][] - list of lists
        vector<Value> seasonal_outer;
        for (auto &seasonal_p : row.seasonal) {
            vector<Value> seasonal_inner;
            for (auto &v : seasonal_p) {
                seasonal_inner.push_back(Value::DOUBLE(v));
            }
            seasonal_outer.push_back(Value::LIST(LogicalType::DOUBLE, seasonal_inner));
        }
        output.data[2].SetValue(i, Value::LIST(LogicalType::LIST(LogicalType::DOUBLE), seasonal_outer));

        // remainder[]
        vector<Value> remainder_values;
        for (auto &v : row.remainder) {
            remainder_values.push_back(Value::DOUBLE(v));
        }
        output.data[3].SetValue(i, Value::LIST(LogicalType::DOUBLE, remainder_values));

        // periods[]
        vector<Value> periods_values;
        for (auto &v : row.periods) {
            periods_values.push_back(Value::INTEGER(v));
        }
        output.data[4].SetValue(i, Value::LIST(LogicalType::INTEGER, periods_values));
    }

    gstate.output_offset += to_output;

    if (gstate.output_offset >= gstate.results.size()) {
        return OperatorFinalizeResultType::FINISHED;
    }
    return OperatorFinalizeResultType::HAVE_MORE_OUTPUT;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterTsMstlDecompositionNativeFunction(ExtensionLoader &loader) {
    // Internal table-in-out function: (TABLE, insufficient_data_mode)
    // Input table must have 3 columns: group_col, date_col, value_col
    // Note: This is an internal function (prefixed with _) called by ts_mstl_decomposition_by macro
    TableFunction func("_ts_mstl_decomposition_native",
        {LogicalType::TABLE, LogicalType::VARCHAR},
        nullptr,  // No execute function - use in_out_function
        TsMstlDecompositionNativeBind,
        TsMstlDecompositionNativeInitGlobal,
        TsMstlDecompositionNativeInitLocal);

    func.in_out_function = TsMstlDecompositionNativeInOut;
    func.in_out_function_final = TsMstlDecompositionNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
