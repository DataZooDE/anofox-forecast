#include "ts_features_native.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include <algorithm>
#include <map>
#include <vector>
#include <cstring>

namespace duckdb {

// ============================================================================
// _ts_features_native - Internal native streaming feature extraction function
//
// This is an INTERNAL function used by ts_features_by macro.
// Users should call ts_features_by() instead of this function directly.
//
// MEMORY FOOTPRINT:
//   - Native (this function): O(input_rows) - buffers per group, processes in finalize
//   - Old SQL macro approach: O(rows) via aggregate function (similar)
//
// The primary benefit is API consistency and preserving original column names.
// ============================================================================

// ============================================================================
// Helper Functions
// ============================================================================

// Get the list of feature names from Rust core (cached)
static const vector<string>& GetFeatureNames() {
    static vector<string> names;
    static bool initialized = false;

    if (!initialized) {
        FeaturesResult result;
        memset(&result, 0, sizeof(result));
        AnofoxError error;

        // Call with a simple series to get feature names
        double dummy[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
        bool success = anofox_ts_features(dummy, 10, &result, &error);

        if (success && result.feature_names && result.n_features > 0) {
            for (size_t i = 0; i < result.n_features; i++) {
                if (result.feature_names[i]) {
                    names.push_back(string(result.feature_names[i]));
                }
            }
            anofox_free_features_result(&result);
        }

        // Fallback minimal set if FFI failed
        if (names.empty()) {
            names = {"length", "mean", "std_dev", "min", "max", "median"};
        }

        initialized = true;
    }

    return names;
}

// ============================================================================
// Bind Data
// ============================================================================

struct TsFeaturesNativeBindData : public TableFunctionData {
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
    string group_col_name = "id";
};

// ============================================================================
// Local State - buffers data per group
// ============================================================================

struct TsFeaturesNativeLocalState : public LocalTableFunctionState {
    // Buffer for incoming data per group
    struct GroupData {
        Value group_value;
        vector<int64_t> timestamps;
        vector<double> values;
    };

    // Map group key -> accumulated data
    std::map<string, GroupData> groups;
    vector<string> group_order;  // Track insertion order

    // Results ready to output
    struct FeatureResult {
        Value group_value;
        vector<double> features;
    };
    vector<FeatureResult> results;
    idx_t current_result = 0;
    bool processed = false;
};

// ============================================================================
// Global State
// ============================================================================

struct TsFeaturesNativeGlobalState : public GlobalTableFunctionState {
    idx_t max_threads = 1;

    idx_t MaxThreads() const override {
        return max_threads;
    }
};

// ============================================================================
// Helper: Get group key from value
// ============================================================================

static string GetGroupKey(const Value &group_value) {
    if (group_value.IsNull()) {
        return "__NULL__";
    }
    return group_value.ToString();
}

// ============================================================================
// Bind Function
// ============================================================================

static unique_ptr<FunctionData> TsFeaturesNativeBind(
    ClientContext &context,
    TableFunctionBindInput &input,
    vector<LogicalType> &return_types,
    vector<string> &names) {

    auto bind_data = make_uniq<TsFeaturesNativeBindData>();

    // Input table must have exactly 3 columns: group, date, value
    if (input.input_table_types.size() != 3) {
        throw InvalidInputException(
            "_ts_features_native requires input with exactly 3 columns: group_col, date_col, value_col. Got %zu columns.",
            input.input_table_types.size());
    }

    // Capture input types
    bind_data->group_logical_type = input.input_table_types[0];
    bind_data->group_col_name = input.input_table_names.size() > 0 ? input.input_table_names[0] : "id";

    // Output schema: group_col (renamed to 'id' for API compatibility) + feature columns
    names.push_back("id");
    return_types.push_back(bind_data->group_logical_type);

    // Add all feature columns
    const auto& feature_names = GetFeatureNames();
    for (const auto& feature_name : feature_names) {
        names.push_back(feature_name);
        return_types.push_back(LogicalType(LogicalTypeId::DOUBLE));
    }

    return bind_data;
}

// ============================================================================
// Init Functions
// ============================================================================

static unique_ptr<GlobalTableFunctionState> TsFeaturesNativeInitGlobal(
    ClientContext &context,
    TableFunctionInitInput &input) {

    auto state = make_uniq<TsFeaturesNativeGlobalState>();
    return state;
}

static unique_ptr<LocalTableFunctionState> TsFeaturesNativeInitLocal(
    ExecutionContext &context,
    TableFunctionInitInput &input,
    GlobalTableFunctionState *global_state) {

    return make_uniq<TsFeaturesNativeLocalState>();
}

// ============================================================================
// In-Out Function - buffers all input rows per group
// ============================================================================

static OperatorResultType TsFeaturesNativeInOut(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &input,
    DataChunk &output) {

    auto &local_state = data.local_state->Cast<TsFeaturesNativeLocalState>();

    // Buffer all input rows
    for (idx_t row_idx = 0; row_idx < input.size(); row_idx++) {
        Value group_val = input.GetValue(0, row_idx);
        Value date_val = input.GetValue(1, row_idx);
        Value value_val = input.GetValue(2, row_idx);

        if (date_val.IsNull()) {
            continue;
        }

        string group_key = GetGroupKey(group_val);

        // Get or create group
        auto it = local_state.groups.find(group_key);
        if (it == local_state.groups.end()) {
            TsFeaturesNativeLocalState::GroupData new_group;
            new_group.group_value = group_val;
            local_state.groups[group_key] = std::move(new_group);
            local_state.group_order.push_back(group_key);
            it = local_state.groups.find(group_key);
        }

        auto &group = it->second;

        // Convert timestamp to int64
        int64_t ts;
        if (date_val.type().id() == LogicalTypeId::TIMESTAMP) {
            ts = date_val.GetValue<timestamp_t>().value;
        } else if (date_val.type().id() == LogicalTypeId::DATE) {
            auto date = date_val.GetValue<date_t>();
            ts = static_cast<int64_t>(date.days) * 24LL * 60LL * 60LL * 1000000LL;
        } else {
            ts = date_val.GetValue<int64_t>();
        }

        group.timestamps.push_back(ts);
        group.values.push_back(value_val.IsNull() ? std::numeric_limits<double>::quiet_NaN() : value_val.GetValue<double>());
    }

    // Don't output anything during input phase
    output.SetCardinality(0);
    return OperatorResultType::NEED_MORE_INPUT;
}

// ============================================================================
// Finalize Function - computes features and outputs results
// ============================================================================

static OperatorFinalizeResultType TsFeaturesNativeFinalize(
    ExecutionContext &context,
    TableFunctionInput &data,
    DataChunk &output) {

    auto &local_state = data.local_state->Cast<TsFeaturesNativeLocalState>();
    const auto& feature_names = GetFeatureNames();

    // Process all groups if not yet done
    if (!local_state.processed) {
        for (const auto& group_key : local_state.group_order) {
            auto& group = local_state.groups[group_key];

            if (group.values.empty()) {
                continue;
            }

            // Sort by timestamp
            vector<pair<int64_t, double>> sorted_pairs;
            for (size_t i = 0; i < group.timestamps.size(); i++) {
                sorted_pairs.push_back({group.timestamps[i], group.values[i]});
            }
            std::sort(sorted_pairs.begin(), sorted_pairs.end());

            vector<double> sorted_values;
            for (const auto& p : sorted_pairs) {
                sorted_values.push_back(p.second);
            }

            // Call feature extraction
            FeaturesResult feat_result;
            memset(&feat_result, 0, sizeof(feat_result));
            AnofoxError error;

            bool success = anofox_ts_features(
                sorted_values.data(),
                sorted_values.size(),
                &feat_result,
                &error
            );

            TsFeaturesNativeLocalState::FeatureResult result;
            result.group_value = group.group_value;
            result.features.resize(feature_names.size(), std::numeric_limits<double>::quiet_NaN());

            if (success && feat_result.n_features > 0) {
                // Build name to value map for quick lookup
                std::map<string, double> feature_map;
                for (size_t i = 0; i < feat_result.n_features; i++) {
                    if (feat_result.feature_names[i]) {
                        feature_map[feat_result.feature_names[i]] = feat_result.features[i];
                    }
                }

                // Fill features in order
                for (size_t i = 0; i < feature_names.size(); i++) {
                    auto it = feature_map.find(feature_names[i]);
                    if (it != feature_map.end()) {
                        result.features[i] = it->second;
                    }
                }

                anofox_free_features_result(&feat_result);
            }

            local_state.results.push_back(std::move(result));
        }

        // Clear input data to free memory
        local_state.groups.clear();
        local_state.group_order.clear();
        local_state.processed = true;
    }

    // Output results
    output.Reset();
    idx_t output_idx = 0;

    while (local_state.current_result < local_state.results.size() && output_idx < STANDARD_VECTOR_SIZE) {
        auto& result = local_state.results[local_state.current_result];

        // Set group value (column 0 = id)
        output.SetValue(0, output_idx, result.group_value);

        // Set feature values (columns 1 to N)
        for (size_t i = 0; i < result.features.size() && i + 1 < output.ColumnCount(); i++) {
            output.SetValue(i + 1, output_idx, Value::DOUBLE(result.features[i]));
        }

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

void RegisterTsFeaturesNativeFunction(ExtensionLoader &loader) {
    // Create the table function with table input
    TableFunction func("_ts_features_native",
                       {LogicalType::TABLE},  // Input table (group, date, value)
                       nullptr,               // main function (unused for in-out)
                       TsFeaturesNativeBind,
                       TsFeaturesNativeInitGlobal,
                       TsFeaturesNativeInitLocal);

    // Set up as table-in-out function
    func.in_out_function = TsFeaturesNativeInOut;
    func.in_out_function_final = TsFeaturesNativeFinalize;

    loader.RegisterFunction(func);
}

} // namespace duckdb
