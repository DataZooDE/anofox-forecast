#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "ts_list_marshal.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/printer.hpp"
#include "duckdb/function/aggregate_function.hpp"
#include "duckdb/common/types/timestamp.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/parser/parsed_data/create_aggregate_function_info.hpp"
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <tuple>
#include <cmath>

namespace duckdb {

// Strict weak ordering on doubles that is safe for std::sort (NaN sorts after
// every number). WR-02: same comparator as ts_stats.cpp's StatsValueLess /
// ts_forecast_agg.cpp's ForecastAggValueLess, used as a deterministic
// secondary sort key for duplicate timestamps.
static bool FeaturesAggValueLess(double x, double y) {
    const bool x_nan = std::isnan(x);
    const bool y_nan = std::isnan(y);
    if (x_nan || y_nan) {
        return !x_nan && y_nan;
    }
    return x < y;
}

// Helper function to emit warnings for unknown feature params
static void EmitFeatureParamWarnings(const vector<string> &param_names) {
    if (param_names.empty()) {
        return;
    }

    // Convert to C string array
    vector<const char*> c_params;
    for (const auto &name : param_names) {
        c_params.push_back(name.c_str());
    }

    char **warnings = nullptr;
    size_t n_warnings = 0;

    if (anofox_ts_validate_feature_params(c_params.data(), c_params.size(), &warnings, &n_warnings)) {
        for (size_t i = 0; i < n_warnings; i++) {
            if (warnings[i]) {
                Printer::Print(StringUtil::Format("Warning: %s", warnings[i]));
            }
        }
        if (warnings) {
            anofox_free_warnings(warnings, n_warnings);
        }
    }
}

// Internal state class (allocated on heap)
struct TsFeaturesAggStateData {
    vector<int64_t> timestamps;
    vector<double> values;
    // Phase 12 D-11: NULL-valued rows are kept (not spliced out) so the Rust
    // single-series policy (ApplySingleSeriesPolicy) can trim edges / reject
    // interior NULLs exactly like ts_features_by. Only a NULL TIMESTAMP skips
    // the row (there is no "position" to record a value at without a date).
    vector<bool> valid;
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
        children.push_back(MakeChild(name, LogicalType(LogicalTypeId::DOUBLE)));
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
            children.push_back(MakeChild(name, LogicalType(LogicalTypeId::DOUBLE)));
        }
    } else {
        // Use selected features in order
        for (const auto &name : selected) {
            children.push_back(MakeChild(name, LogicalType(LogicalTypeId::DOUBLE)));
        }
    }

    return LogicalType::STRUCT(std::move(children));
}

// Bind function for 3-parameter version (with feature_selection)
static unique_ptr<FunctionData> TsFeaturesAggBind3(ClientContext &context, AggregateBindFunction &function,
                                                    vector<unique_ptr<Expression>> &arguments) {
    auto bind_data = make_uniq<TsFeaturesAggBindData>();

    // Third argument is feature_selection (LIST(VARCHAR) or NULL)
    if (arguments.size() >= 3 && ReturnTypeOf(*arguments[2]).id() != LogicalTypeId::SQLNULL) {
        bind_data->has_feature_selection = true;
        // The actual feature list will be extracted at runtime
        // For now, we return all features - dynamic typing based on runtime values
        // would require more complex bind logic
    }

    // Set return type to all features (filtering happens at runtime)
    SetReturnTypeOf(function, GetFeaturesAggResultType());

    return bind_data;
}

// Bind function for 4-parameter version (with feature_selection and feature_params)
static unique_ptr<FunctionData> TsFeaturesAggBind4(ClientContext &context, AggregateBindFunction &function,
                                                    vector<unique_ptr<Expression>> &arguments) {
    auto bind_data = make_uniq<TsFeaturesAggBindData>();

    // Third argument is feature_selection, fourth is feature_params
    if (arguments.size() >= 3 && ReturnTypeOf(*arguments[2]).id() != LogicalTypeId::SQLNULL) {
        bind_data->has_feature_selection = true;
    }
    // feature_params (4th arg) is accepted but not currently used

    SetReturnTypeOf(function, GetFeaturesAggResultType());

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
            target.data->valid.insert(target.data->valid.end(),
                                      source.data->valid.begin(),
                                      source.data->valid.end());
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
    ToUnified(ts_vec, count, ts_data);
    ToUnified(val_vec, count, val_data);

    // Window evaluation passes a constant or dictionary state vector, GROUP BY a
    // flat one: read it through UnifiedVectorFormat so both are handled.
    UnifiedVectorFormat state_data;
    ToUnified(state_vector, count, state_data);
    auto states = UnifiedVectorFormat::GetData<TsFeaturesAggState *>(state_data);

    for (idx_t i = 0; i < count; i++) {
        auto &state = *states[state_data.sel->get_index(i)];

        auto ts_idx = ts_data.sel->get_index(i);
        auto val_idx = val_data.sel->get_index(i);

        // Phase 12 D-11: a NULL TIMESTAMP has no position to record a value
        // at, so it is still skipped. A NULL VALUE is kept (valid=false) so
        // the Rust gate can see it and apply the single-series policy — this
        // is the fix for the old splice-NULL-rows bug.
        if (!ts_data.validity.RowIsValid(ts_idx)) {
            continue;
        }

        if (!state.data) {
            state.data = new TsFeaturesAggStateData();
        }

        if (!state.data->initialized) {
            state.data->initialized = true;
        }

        auto ts = UnifiedVectorFormat::GetData<timestamp_t>(ts_data)[ts_idx];
        bool value_valid = val_data.validity.RowIsValid(val_idx);
        double val = value_valid ? UnifiedVectorFormat::GetData<double>(val_data)[val_idx] : 0.0;

        state.data->timestamps.push_back(ts.value);
        state.data->values.push_back(val);
        state.data->valid.push_back(value_valid);
    }
}

static void TsFeaturesAggFinalize(Vector &state_vector, AggregateInputData &aggr_input,
                                  Vector &result, idx_t count, idx_t offset) {
    auto states = FlatDataMut<TsFeaturesAggState *>(state_vector);

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

        // Sort (timestamp, value, valid) triples together by timestamp (D-11:
        // the validity bitmask must stay aligned with the values it describes).
        // WR-02: duplicate timestamps are tie-broken deterministically (valid
        // before NULL, then by value with NaN last) — the same comparator
        // ts_stats.cpp's StatsValueLess and ts_forecast_agg.cpp's
        // ForecastAggValueLess use — so the result never depends on the order
        // partial aggregate states were concatenated in Combine() (itself
        // dependent on thread scheduling); std::sort by timestamp alone left
        // the relative order of same-timestamp rows unspecified.
        vector<std::tuple<int64_t, double, bool>> sorted_triples;
        sorted_triples.reserve(data.timestamps.size());
        for (size_t j = 0; j < data.timestamps.size(); j++) {
            sorted_triples.push_back({data.timestamps[j], data.values[j], data.valid[j]});
        }
        std::sort(sorted_triples.begin(), sorted_triples.end(),
                  [](const std::tuple<int64_t, double, bool> &a, const std::tuple<int64_t, double, bool> &b) {
                      if (std::get<0>(a) != std::get<0>(b)) {
                          return std::get<0>(a) < std::get<0>(b);
                      }
                      const bool a_valid = std::get<2>(a);
                      const bool b_valid = std::get<2>(b);
                      if (a_valid != b_valid) {
                          return a_valid;
                      }
                      return a_valid && FeaturesAggValueLess(std::get<1>(a), std::get<1>(b));
                  });

        vector<double> sorted_values;
        sorted_values.reserve(sorted_triples.size());
        size_t validity_words = (sorted_triples.size() + 63) / 64;
        vector<uint64_t> sorted_validity(validity_words, 0);
        for (size_t j = 0; j < sorted_triples.size(); j++) {
            sorted_values.push_back(std::get<1>(sorted_triples[j]));
            if (std::get<2>(sorted_triples[j])) {
                sorted_validity[j / 64] |= (1ULL << (j % 64));
            }
        }

        // Phase 12 D-03/D-04/D-11: Rust single-series gate — trims leading/
        // trailing NULLs, errors on an interior NULL naming its position,
        // NULL result if nothing remains after trimming.
        if (!ApplySingleSeriesPolicy(sorted_values, sorted_validity, "ts_features_agg")) {
            FlatVector::SetNull(result, row, true);
            continue;
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
            auto &child_vec = StructEntry(struct_entries[j]);
            auto child_data = FlatDataMut<double>(child_vec);

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
    auto src_states = FlatDataMut<TsFeaturesAggState *>(state_vector);
    auto tgt_states = FlatDataMut<TsFeaturesAggState *>(combined);

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
            tgt.data->valid.insert(tgt.data->valid.end(),
                                   src.data->valid.begin(),
                                   src.data->valid.end());
        }
    }
}

static void TsFeaturesAggDestructor(Vector &state_vector, AggregateInputData &aggr_input, idx_t count) {
    auto states = FlatDataMut<TsFeaturesAggState *>(state_vector);
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
        {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE)},
        GetFeaturesAggResultType(),
        AggregateFunction::StateSize<TsFeaturesAggState>,
        AggregateFunction::StateInitialize<TsFeaturesAggState, TsFeaturesAggOperation>,
        TsFeaturesAggUpdate,
        TsFeaturesAggCombine,
        ANOFOX_AGG_FINALIZE(TsFeaturesAggFinalize),
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
        {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE), LogicalType::LIST(LogicalType(LogicalTypeId::VARCHAR))},
        GetFeaturesAggResultType(),
        AggregateFunction::StateSize<TsFeaturesAggState>,
        AggregateFunction::StateInitialize<TsFeaturesAggState, TsFeaturesAggOperation>,
        TsFeaturesAggUpdate,
        TsFeaturesAggCombine,
        ANOFOX_AGG_FINALIZE(TsFeaturesAggFinalize),
        nullptr,  // simple_update
        ANOFOX_AGG_BIND(TsFeaturesAggBind3),
        TsFeaturesAggDestructor
    );

    // =========================================================================
    // 4-parameter version: ts_features_agg(ts, val, feature_selection, feature_params)
    // feature_selection: LIST(VARCHAR) or NULL
    // feature_params: LIST(STRUCT(feature VARCHAR, params_json VARCHAR)) or NULL
    // =========================================================================
    // Define the feature_params type: LIST(STRUCT(feature VARCHAR, params_json VARCHAR))
    child_list_t<LogicalType> param_struct_children;
    param_struct_children.push_back(make_pair("feature", LogicalType(LogicalTypeId::VARCHAR)));
    param_struct_children.push_back(make_pair("params_json", LogicalType(LogicalTypeId::VARCHAR)));
    auto param_struct_type = LogicalType::STRUCT(std::move(param_struct_children));

    AggregateFunction agg_func_4(
        "ts_features_agg",
        {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE),
         LogicalType::LIST(LogicalType(LogicalTypeId::VARCHAR)),
         LogicalType::LIST(param_struct_type)},
        GetFeaturesAggResultType(),
        AggregateFunction::StateSize<TsFeaturesAggState>,
        AggregateFunction::StateInitialize<TsFeaturesAggState, TsFeaturesAggOperation>,
        TsFeaturesAggUpdate,
        TsFeaturesAggCombine,
        ANOFOX_AGG_FINALIZE(TsFeaturesAggFinalize),
        nullptr,  // simple_update
        ANOFOX_AGG_BIND(TsFeaturesAggBind4),
        TsFeaturesAggDestructor
    );

    // Register ts_features as aggregate function (C++ API compatible)
    // C++ only has ts_features, not ts_features_agg
    AggregateFunctionSet ts_features_set("ts_features");
    ts_features_set.AddFunction(agg_func_2);
    ts_features_set.AddFunction(agg_func_3);
    ts_features_set.AddFunction(agg_func_4);
    {
        CreateAggregateFunctionInfo info(ts_features_set);
        {
            FunctionDescription desc;
            desc.description = "Aggregate function: extracts 117 tsfresh-compatible time series features for each group. Returns a STRUCT with all computed features.";
            desc.examples = {"SELECT product_id, ts_features(date, value) FROM sales GROUP BY product_id"};
            desc.categories = {"time-series", "feature-extraction"};
            desc.parameter_names = {"date", "value"};
            desc.parameter_types = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE)};
            info.descriptions.push_back(std::move(desc));
        }
        {
            FunctionDescription desc;
            desc.description = "Aggregate function: extracts 117 tsfresh-compatible time series features for each group. Returns a STRUCT with all computed features.";
            desc.examples = {"SELECT product_id, ts_features(date, value, ['mean', 'std_dev']) FROM sales GROUP BY product_id"};
            desc.categories = {"time-series", "feature-extraction"};
            desc.parameter_names = {"date", "value", "feature_selection"};
            desc.parameter_types = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE), LogicalType::LIST(LogicalType(LogicalTypeId::VARCHAR))};
            info.descriptions.push_back(std::move(desc));
        }
        {
            FunctionDescription desc;
            desc.description = "Aggregate function: extracts 117 tsfresh-compatible time series features for each group. Returns a STRUCT with all computed features.";
            desc.examples = {"SELECT product_id, ts_features(date, value, ['mean'], NULL) FROM sales GROUP BY product_id"};
            desc.categories = {"time-series", "feature-extraction"};
            desc.parameter_names = {"date", "value", "feature_selection", "feature_params"};
            desc.parameter_types = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE), LogicalType::LIST(LogicalType(LogicalTypeId::VARCHAR)), LogicalType::LIST(param_struct_type)};
            info.descriptions.push_back(std::move(desc));
        }
        loader.RegisterFunction(std::move(info));
    }

    // Register ts_features_agg alias (for API consistency with ts_forecast_agg pattern)
    AggregateFunctionSet ts_features_agg_set("ts_features_agg");
    ts_features_agg_set.AddFunction(agg_func_2);
    ts_features_agg_set.AddFunction(agg_func_3);
    ts_features_agg_set.AddFunction(agg_func_4);
    {
        CreateAggregateFunctionInfo info(ts_features_agg_set);
        {
            FunctionDescription desc;
            desc.description = "Aggregate function: extracts 117 tsfresh-compatible time series features for each group. Returns a STRUCT with all computed features.";
            desc.examples = {"SELECT product_id, ts_features_agg(date, value) FROM sales GROUP BY product_id"};
            desc.categories = {"time-series", "feature-extraction"};
            desc.parameter_names = {"date", "value"};
            desc.parameter_types = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE)};
            info.descriptions.push_back(std::move(desc));
        }
        {
            FunctionDescription desc;
            desc.description = "Aggregate function: extracts 117 tsfresh-compatible time series features for each group. Returns a STRUCT with all computed features.";
            desc.examples = {"SELECT product_id, ts_features_agg(date, value, config) FROM sales GROUP BY product_id"};
            desc.categories = {"time-series", "feature-extraction"};
            desc.parameter_names = {"date", "value", "feature_selection"};
            desc.parameter_types = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE), LogicalType::LIST(LogicalType(LogicalTypeId::VARCHAR))};
            info.descriptions.push_back(std::move(desc));
        }
        {
            FunctionDescription desc;
            desc.description = "Aggregate function: extracts 117 tsfresh-compatible time series features for each group. Returns a STRUCT with all computed features.";
            desc.examples = {"SELECT product_id, ts_features_agg(date, value, ['mean'], NULL) FROM sales GROUP BY product_id"};
            desc.categories = {"time-series", "feature-extraction"};
            desc.parameter_names = {"date", "value", "feature_selection", "feature_params"};
            desc.parameter_types = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE), LogicalType::LIST(LogicalType(LogicalTypeId::VARCHAR)), LogicalType::LIST(param_struct_type)};
            info.descriptions.push_back(std::move(desc));
        }
        loader.RegisterFunction(std::move(info));
    }

    // Register anofox_fcst_ts_features_agg alias (prefixed form)
    AggregateFunctionSet anofox_features_agg_set("anofox_fcst_ts_features_agg");
    anofox_features_agg_set.AddFunction(agg_func_2);
    anofox_features_agg_set.AddFunction(agg_func_3);
    anofox_features_agg_set.AddFunction(agg_func_4);
    {
        CreateAggregateFunctionInfo info(anofox_features_agg_set);
        info.alias_of = "ts_features_agg";
        {
            FunctionDescription desc;
            desc.description = "Aggregate function: extracts 117 tsfresh-compatible time series features for each group. Returns a STRUCT with all computed features.";
            desc.examples = {"SELECT product_id, anofox_fcst_ts_features_agg(date, value) FROM sales GROUP BY product_id"};
            desc.categories = {"time-series", "feature-extraction"};
            desc.parameter_names = {"date", "value"};
            desc.parameter_types = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE)};
            info.descriptions.push_back(std::move(desc));
        }
        {
            FunctionDescription desc;
            desc.description = "Aggregate function: extracts 117 tsfresh-compatible time series features for each group. Returns a STRUCT with all computed features.";
            desc.examples = {"SELECT product_id, anofox_fcst_ts_features_agg(date, value, config) FROM sales GROUP BY product_id"};
            desc.categories = {"time-series", "feature-extraction"};
            desc.parameter_names = {"date", "value", "feature_selection"};
            desc.parameter_types = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE), LogicalType::LIST(LogicalType(LogicalTypeId::VARCHAR))};
            info.descriptions.push_back(std::move(desc));
        }
        {
            FunctionDescription desc;
            desc.description = "Aggregate function: extracts 117 tsfresh-compatible time series features for each group. Returns a STRUCT with all computed features.";
            desc.examples = {"SELECT product_id, anofox_fcst_ts_features_agg(date, value, ['mean'], NULL) FROM sales GROUP BY product_id"};
            desc.categories = {"time-series", "feature-extraction"};
            desc.parameter_names = {"date", "value", "feature_selection", "feature_params"};
            desc.parameter_types = {LogicalType(LogicalTypeId::TIMESTAMP), LogicalType(LogicalTypeId::DOUBLE), LogicalType::LIST(LogicalType(LogicalTypeId::VARCHAR)), LogicalType::LIST(param_struct_type)};
            info.descriptions.push_back(std::move(desc));
        }
        loader.RegisterFunction(std::move(info));
    }
}

} // namespace duckdb
