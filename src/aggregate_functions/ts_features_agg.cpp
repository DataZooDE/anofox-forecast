#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include <unordered_map>
#include <unordered_set>
#include <limits>

namespace duckdb {

// Internal state class (allocated on heap)
struct TsFeaturesAggStateData {
    vector<int64_t> timestamps;
    vector<double> values;
    bool initialized;

    TsFeaturesAggStateData() : initialized(false) {}
};

// Trivially constructible state wrapper (just a pointer)
struct TsFeaturesAggState {
    TsFeaturesAggStateData *data;
};

// Bind data for feature selection
struct TsFeaturesAggBindData : public FunctionData {
    vector<string> selected_features;  // Empty means all features
    bool has_feature_selection;

    TsFeaturesAggBindData() : has_feature_selection(false) {}

    unique_ptr<FunctionData> Copy() const override {
        auto result = make_uniq<TsFeaturesAggBindData>();
        result->selected_features = selected_features;
        result->has_feature_selection = has_feature_selection;
        return result;
    }

    bool Equals(const FunctionData &other_p) const override {
        auto &other = other_p.Cast<TsFeaturesAggBindData>();
        return selected_features == other.selected_features &&
               has_feature_selection == other.has_feature_selection;
    }
};

// Get the list of feature names from Rust core
static vector<string> GetFeatureNames() {
    vector<string> names;

    // Get feature names from FFI
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

    // If FFI call failed, return a minimal set
    if (names.empty()) {
        names = {"length", "mean", "std_dev", "min", "max", "median"};
    }

    return names;
}

static LogicalType GetFeaturesAggResultType() {
    // Build a STRUCT with all feature names as columns
    child_list_t<LogicalType> children;

    auto feature_names = GetFeatureNames();
    for (const auto &name : feature_names) {
        children.push_back(make_pair(name, LogicalType::DOUBLE));
    }

    return LogicalType::STRUCT(std::move(children));
}

// Build result type for selected features only
static LogicalType GetSelectedFeaturesResultType(const vector<string> &selected) {
    child_list_t<LogicalType> children;

    if (selected.empty()) {
        // No selection means all features
        auto all_features = GetFeatureNames();
        for (const auto &name : all_features) {
            children.push_back(make_pair(name, LogicalType::DOUBLE));
        }
    } else {
        // Use selected features in order
        for (const auto &name : selected) {
            children.push_back(make_pair(name, LogicalType::DOUBLE));
        }
    }

    return LogicalType::STRUCT(std::move(children));
}

// Bind function for 3-parameter version (with feature_selection)
static unique_ptr<FunctionData> TsFeaturesAggBind3(ClientContext &context, AggregateFunction &function,
                                                    vector<unique_ptr<Expression>> &arguments) {
    auto bind_data = make_uniq<TsFeaturesAggBindData>();

    // Third argument is feature_selection (LIST(VARCHAR) or NULL)
    if (arguments.size() >= 3 && arguments[2]->return_type.id() != LogicalTypeId::SQLNULL) {
        bind_data->has_feature_selection = true;
        // The actual feature list will be extracted at runtime
        // For now, we return all features - dynamic typing based on runtime values
        // would require more complex bind logic
    }

    // Set return type to all features (filtering happens at runtime)
    function.return_type = GetFeaturesAggResultType();

    return bind_data;
}

// Bind function for 4-parameter version (with feature_selection and feature_params)
static unique_ptr<FunctionData> TsFeaturesAggBind4(ClientContext &context, AggregateFunction &function,
                                                    vector<unique_ptr<Expression>> &arguments) {
    auto bind_data = make_uniq<TsFeaturesAggBindData>();

    // Third argument is feature_selection, fourth is feature_params
    if (arguments.size() >= 3 && arguments[2]->return_type.id() != LogicalTypeId::SQLNULL) {
        bind_data->has_feature_selection = true;
    }
    // feature_params (4th arg) is accepted but not currently used

    function.return_type = GetFeaturesAggResultType();

    return bind_data;
}

struct TsFeaturesAggOperation {
    template <class STATE>
    static void Initialize(STATE &state) {
        state.data = nullptr;
    }

    template <class STATE, class OP>
    static void Combine(const STATE &source, STATE &target, AggregateInputData &) {
        if (!source.data || !source.data->initialized) {
            return;
        }
        if (!target.data) {
            target.data = new TsFeaturesAggStateData();
        }
        if (!target.data->initialized) {
            *target.data = *source.data;
        } else {
            target.data->timestamps.insert(target.data->timestamps.end(),
                                           source.data->timestamps.begin(),
                                           source.data->timestamps.end());
            target.data->values.insert(target.data->values.end(),
                                       source.data->values.begin(),
                                       source.data->values.end());
        }
    }

    template <class T, class STATE>
    static void Finalize(STATE &state, T &target, AggregateFinalizeData &finalize_data) {
        // Not used - we use custom finalize
        finalize_data.ReturnNull();
    }

    static bool IgnoreNull() {
        return true;
    }
};

static void TsFeaturesAggUpdate(Vector inputs[], AggregateInputData &aggr_input, idx_t input_count,
                                Vector &state_vector, idx_t count) {
    auto &ts_vec = inputs[0];
    auto &val_vec = inputs[1];

    UnifiedVectorFormat ts_data, val_data;
    ts_vec.ToUnifiedFormat(count, ts_data);
    val_vec.ToUnifiedFormat(count, val_data);

    auto states = FlatVector::GetData<TsFeaturesAggState *>(state_vector);

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];

        auto ts_idx = ts_data.sel->get_index(i);
        auto val_idx = val_data.sel->get_index(i);

        if (!ts_data.validity.RowIsValid(ts_idx) || !val_data.validity.RowIsValid(val_idx)) {
            continue;
        }

        if (!state.data) {
            state.data = new TsFeaturesAggStateData();
        }

        if (!state.data->initialized) {
            state.data->initialized = true;
        }

        auto ts = UnifiedVectorFormat::GetData<timestamp_t>(ts_data)[ts_idx];
        auto val = UnifiedVectorFormat::GetData<double>(val_data)[val_idx];

        state.data->timestamps.push_back(ts.value);
        state.data->values.push_back(val);
    }
}

static void TsFeaturesAggFinalize(Vector &state_vector, AggregateInputData &aggr_input,
                                  Vector &result, idx_t count, idx_t offset) {
    auto states = FlatVector::GetData<TsFeaturesAggState *>(state_vector);

    // Get feature names for struct field lookup
    static auto feature_names = GetFeatureNames();

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[i];
        idx_t row = i + offset;

        if (!state.data || !state.data->initialized || state.data->values.empty()) {
            FlatVector::SetNull(result, row, true);
            continue;
        }

        auto &data = *state.data;

        // Sort by timestamp
        vector<pair<int64_t, double>> sorted_pairs;
        for (size_t j = 0; j < data.timestamps.size(); j++) {
            sorted_pairs.push_back({data.timestamps[j], data.values[j]});
        }
        std::sort(sorted_pairs.begin(), sorted_pairs.end());

        vector<double> sorted_values;
        for (const auto &p : sorted_pairs) {
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

        if (!success) {
            FlatVector::SetNull(result, row, true);
            continue;
        }

        // Build name to value map for quick lookup
        std::unordered_map<string, double> feature_map;
        for (size_t j = 0; j < feat_result.n_features; j++) {
            if (feat_result.feature_names[j]) {
                feature_map[feat_result.feature_names[j]] = feat_result.features[j];
            }
        }

        // Populate the struct fields
        auto &struct_entries = StructVector::GetEntries(result);
        for (size_t j = 0; j < feature_names.size() && j < struct_entries.size(); j++) {
            auto &child_vec = *struct_entries[j];
            auto child_data = FlatVector::GetData<double>(child_vec);

            auto it = feature_map.find(feature_names[j]);
            if (it != feature_map.end()) {
                child_data[row] = it->second;
            } else {
                child_data[row] = std::numeric_limits<double>::quiet_NaN();
            }
        }

        anofox_free_features_result(&feat_result);
    }
}

static void TsFeaturesAggCombine(Vector &state_vector, Vector &combined, AggregateInputData &aggr_input,
                                 idx_t count) {
    auto src_states = FlatVector::GetData<TsFeaturesAggState *>(state_vector);
    auto tgt_states = FlatVector::GetData<TsFeaturesAggState *>(combined);

    for (idx_t i = 0; i < count; i++) {
        auto &src = *src_states[i];
        auto &tgt = *tgt_states[i];

        if (!src.data || !src.data->initialized) {
            continue;
        }

        if (!tgt.data) {
            tgt.data = new TsFeaturesAggStateData();
            *tgt.data = *src.data;
        } else if (!tgt.data->initialized) {
            *tgt.data = *src.data;
        } else {
            tgt.data->timestamps.insert(tgt.data->timestamps.end(),
                                        src.data->timestamps.begin(),
                                        src.data->timestamps.end());
            tgt.data->values.insert(tgt.data->values.end(),
                                    src.data->values.begin(),
                                    src.data->values.end());
        }
    }
}

static void TsFeaturesAggDestructor(Vector &state_vector, AggregateInputData &aggr_input, idx_t count) {
    auto states = FlatVector::GetData<TsFeaturesAggState *>(state_vector);
    for (idx_t i = 0; i < count; i++) {
        if (states[i] && states[i]->data) {
            delete states[i]->data;
            states[i]->data = nullptr;
        }
    }
}

void RegisterTsFeaturesAggFunction(ExtensionLoader &loader) {
    // =========================================================================
    // 2-parameter version: ts_features_agg(ts_column, value_column)
    // =========================================================================
    AggregateFunction agg_func_2(
        "ts_features_agg",
        {LogicalType::TIMESTAMP, LogicalType::DOUBLE},
        GetFeaturesAggResultType(),
        AggregateFunction::StateSize<TsFeaturesAggState>,
        AggregateFunction::StateInitialize<TsFeaturesAggState, TsFeaturesAggOperation>,
        TsFeaturesAggUpdate,
        TsFeaturesAggCombine,
        TsFeaturesAggFinalize,
        nullptr,  // simple_update
        nullptr,  // bind
        TsFeaturesAggDestructor
    );

    // =========================================================================
    // 3-parameter version: ts_features_agg(ts_column, value_column, feature_selection)
    // feature_selection: LIST(VARCHAR) or NULL
    // =========================================================================
    AggregateFunction agg_func_3(
        "ts_features_agg",
        {LogicalType::TIMESTAMP, LogicalType::DOUBLE, LogicalType::LIST(LogicalType::VARCHAR)},
        GetFeaturesAggResultType(),
        AggregateFunction::StateSize<TsFeaturesAggState>,
        AggregateFunction::StateInitialize<TsFeaturesAggState, TsFeaturesAggOperation>,
        TsFeaturesAggUpdate,
        TsFeaturesAggCombine,
        TsFeaturesAggFinalize,
        nullptr,  // simple_update
        TsFeaturesAggBind3,
        TsFeaturesAggDestructor
    );

    // =========================================================================
    // 4-parameter version: ts_features_agg(ts, val, feature_selection, feature_params)
    // feature_selection: LIST(VARCHAR) or NULL
    // feature_params: LIST(STRUCT(feature VARCHAR, params_json VARCHAR)) or NULL
    // =========================================================================
    // Define the feature_params type: LIST(STRUCT(feature VARCHAR, params_json VARCHAR))
    child_list_t<LogicalType> param_struct_children;
    param_struct_children.push_back(make_pair("feature", LogicalType::VARCHAR));
    param_struct_children.push_back(make_pair("params_json", LogicalType::VARCHAR));
    auto param_struct_type = LogicalType::STRUCT(std::move(param_struct_children));

    AggregateFunction agg_func_4(
        "ts_features_agg",
        {LogicalType::TIMESTAMP, LogicalType::DOUBLE,
         LogicalType::LIST(LogicalType::VARCHAR),
         LogicalType::LIST(param_struct_type)},
        GetFeaturesAggResultType(),
        AggregateFunction::StateSize<TsFeaturesAggState>,
        AggregateFunction::StateInitialize<TsFeaturesAggState, TsFeaturesAggOperation>,
        TsFeaturesAggUpdate,
        TsFeaturesAggCombine,
        TsFeaturesAggFinalize,
        nullptr,  // simple_update
        TsFeaturesAggBind4,
        TsFeaturesAggDestructor
    );

    // Register all overloads for ts_features_agg
    AggregateFunctionSet func_set("ts_features_agg");
    func_set.AddFunction(agg_func_2);
    func_set.AddFunction(agg_func_3);
    func_set.AddFunction(agg_func_4);
    loader.RegisterFunction(func_set);

    // Also register with anofox_fcst_ prefix
    AggregateFunctionSet alias_set("anofox_fcst_ts_features_agg");
    alias_set.AddFunction(agg_func_2);
    alias_set.AddFunction(agg_func_3);
    alias_set.AddFunction(agg_func_4);
    loader.RegisterFunction(alias_set);

    // =========================================================================
    // Register ts_features as aggregate function (C++ API compatible)
    // This allows: ts_features(ts_col, val_col, feature_selection, feature_params)
    // The scalar ts_features(DOUBLE[]) is registered separately in ts_features.cpp
    // =========================================================================
    AggregateFunctionSet ts_features_agg_set("ts_features");
    ts_features_agg_set.AddFunction(agg_func_2);
    ts_features_agg_set.AddFunction(agg_func_3);
    ts_features_agg_set.AddFunction(agg_func_4);
    loader.RegisterFunction(ts_features_agg_set);

    AggregateFunctionSet anofox_ts_features_set("anofox_fcst_ts_features");
    anofox_ts_features_set.AddFunction(agg_func_2);
    anofox_ts_features_set.AddFunction(agg_func_3);
    anofox_ts_features_set.AddFunction(agg_func_4);
    loader.RegisterFunction(anofox_ts_features_set);
}

} // namespace duckdb
