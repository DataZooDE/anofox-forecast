#include "ts_mstl_decomposition_native.hpp"
#include "ts_fill_gaps_native.hpp"  // For GetGroupKey, etc.
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <cstring>

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
// Local State - buffers data per thread and manages streaming output
// ============================================================================

struct TsMstlDecompositionNativeLocalState : public LocalTableFunctionState {
    // Input data buffer per group
    struct GroupData {
        Value group_value;
        vector<int64_t> dates;  // microseconds for sorting
        vector<double> values;
    };

    std::map<string, GroupData> groups;
    vector<string> group_order;

    // Output results
    struct DecompositionOutputRow {
        Value group_value;
        vector<double> trend;
        vector<vector<double>> seasonal;  // one array per period
        vector<double> remainder;
        vector<int32_t> periods;
    };
    vector<DecompositionOutputRow> results;

    // Processing state
    bool processed = false;
    idx_t output_offset = 0;
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

    // Output schema: id, trend[], seasonal[][], remainder[], periods[]
    names.push_back("id");
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
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsMstlDecompositionNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {
    return make_uniq<GlobalTableFunctionState>();
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

    auto &local_state = data_p.local_state->Cast<TsMstlDecompositionNativeLocalState>();

    // Buffer all incoming data - we need complete groups
    // Input columns: group_col, date_col, value_col
    for (idx_t i = 0; i < input.size(); i++) {
        Value group_val = input.data[0].GetValue(i);
        Value date_val = input.data[1].GetValue(i);
        Value value_val = input.data[2].GetValue(i);

        if (date_val.IsNull()) continue;

        string group_key = GetGroupKey(group_val);

        if (local_state.groups.find(group_key) == local_state.groups.end()) {
            local_state.groups[group_key] = TsMstlDecompositionNativeLocalState::GroupData();
            local_state.groups[group_key].group_value = group_val;
            local_state.group_order.push_back(group_key);
        }

        auto &grp = local_state.groups[group_key];

        // Get date as microseconds for sorting
        int64_t date_micros = 0;
        if (date_val.type().id() == LogicalTypeId::TIMESTAMP) {
            date_micros = TimestampToMicroseconds(date_val.GetValue<timestamp_t>());
        } else if (date_val.type().id() == LogicalTypeId::DATE) {
            date_micros = DateToMicroseconds(date_val.GetValue<date_t>());
        } else if (date_val.type().id() == LogicalTypeId::BIGINT) {
            date_micros = date_val.GetValue<int64_t>();
        } else if (date_val.type().id() == LogicalTypeId::INTEGER) {
            date_micros = date_val.GetValue<int32_t>();
        }

        grp.dates.push_back(date_micros);
        grp.values.push_back(value_val.IsNull() ? 0.0 : value_val.GetValue<double>());
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
    auto &local_state = data_p.local_state->Cast<TsMstlDecompositionNativeLocalState>();

    // Process all groups on first finalize call
    if (!local_state.processed) {
        for (const auto &group_key : local_state.group_order) {
            auto &grp = local_state.groups[group_key];

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

            TsMstlDecompositionNativeLocalState::DecompositionOutputRow row;
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

            local_state.results.push_back(row);
        }

        local_state.processed = true;
    }

    // Output results in batches
    idx_t remaining = local_state.results.size() - local_state.output_offset;
    if (remaining == 0) {
        output.SetCardinality(0);
        return OperatorFinalizeResultType::FINISHED;
    }

    idx_t to_output = std::min(remaining, static_cast<idx_t>(STANDARD_VECTOR_SIZE));
    output.SetCardinality(to_output);

    for (idx_t i = 0; i < to_output; i++) {
        auto &row = local_state.results[local_state.output_offset + i];

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

    local_state.output_offset += to_output;

    if (local_state.output_offset >= local_state.results.size()) {
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
