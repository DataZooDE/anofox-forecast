#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/common/types/vector.hpp"
#include <unordered_map>
#include <limits>
#include "anofox_forecast_banner.hpp"

namespace duckdb {

// ============================================================================
// ts_features - the scalar-function body that lived here (TsFeaturesFunction)
// was never reachable from SQL (C++ extension exposes ts_features only as the
// aggregate registered in ts_features_agg.cpp). Deleted per Phase 12 D-01/D-11
// plan 12-03 — its private ExtractListAsDouble/GetScalarFeatureNames/
// GetScalarFeaturesResultType helpers were used only by this dead body. The
// no-op scalar registration function for this never-registered body was
// removed in plan 12-07 (D-09).
// ============================================================================

// ============================================================================
// ts_features_list - List available features as TABLE
// C++ API compatible: Returns TABLE(column_name, feature_name, parameter_suffix, default_parameters, parameter_keys)
// ============================================================================

// Per-execution state: the feature-name snapshot and output cursor live here,
// NOT in bind data (bind data is immutable and shared across every execution
// of a prepared statement -- see #289 BUG-02's second finding: a cursor kept
// in bind data meant EXECUTE on the same prepared statement returned 117 rows
// then 0 on the next EXECUTE). InitGlobal fetches and releases the FFI array
// exactly once per query execution; Execute only ever reads this state.
struct TsFeaturesListGlobalState : public GlobalTableFunctionState {
    vector<string> feature_names;
    idx_t current_idx = 0;
};

static unique_ptr<FunctionData> TsFeaturesListBind(ClientContext &context, TableFunctionBindInput &input,
                                                    vector<LogicalType> &return_types, TableBindNames &names) {
    // Define return columns to match C++ extension
    names.push_back(MakeBindName("column_name"));
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    names.push_back(MakeBindName("feature_name"));
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    names.push_back(MakeBindName("parameter_suffix"));
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    names.push_back(MakeBindName("default_parameters"));
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    names.push_back(MakeBindName("parameter_keys"));
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    return make_uniq<TableFunctionData>();
}

static unique_ptr<GlobalTableFunctionState> TsFeaturesListInitGlobal(ClientContext &context,
                                                                      TableFunctionInitInput &input) {
    auto state = make_uniq<TsFeaturesListGlobalState>();

    // Get feature names from FFI, exactly once per execution.
    char *names_raw = nullptr;
    size_t n_names = 0;
    anofox_ts_features_list(&names_raw, &n_names);
    char **names = reinterpret_cast<char**>(names_raw);

    if (names) {
        state->feature_names.reserve(n_names);
        for (size_t i = 0; i < n_names; i++) {
            if (names[i]) {
                state->feature_names.push_back(names[i]);
            }
        }
        anofox_free_ts_features_list(names, n_names);
    }

    return std::move(state);
}

static void TsFeaturesListExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
    auto &gstate = data_p.global_state->Cast<TsFeaturesListGlobalState>();

    idx_t count = 0;
    idx_t max_count = STANDARD_VECTOR_SIZE;

    while (gstate.current_idx < gstate.feature_names.size() && count < max_count) {
        const auto &name = gstate.feature_names[gstate.current_idx];

        // column_name: "value" (default column name for features)
        FlatDataMut<string_t>(output.data[0])[count] =
            StringVector::AddString(output.data[0], "value");

        // feature_name: the actual feature name
        FlatDataMut<string_t>(output.data[1])[count] =
            StringVector::AddString(output.data[1], name);

        // parameter_suffix: empty for basic features
        FlatDataMut<string_t>(output.data[2])[count] =
            StringVector::AddString(output.data[2], "");

        // default_parameters: empty for basic features
        FlatDataMut<string_t>(output.data[3])[count] =
            StringVector::AddString(output.data[3], "{}");

        // parameter_keys: empty for basic features
        FlatDataMut<string_t>(output.data[4])[count] =
            StringVector::AddString(output.data[4], "");

        gstate.current_idx++;
        count++;
    }

    SetOutputCardinality(output, count);
}

void RegisterTsFeaturesListFunction(ExtensionLoader &loader) {
    // NOT wrapped in DATAZOO_GUARD (#289 BUG-02, bisected empirically): this
    // extension's only other DATAZOO_GUARD-wrapped function pair below
    // (ts_features_config_template) has Bind/Execute/InitGlobal signatures
    // identical to this function's. When BOTH catalog functions' bind/execute
    // pairs were DATAZOO_GUARD-wrapped together, the wasm_eh build traps with
    // "memory access out of bounds" in DuckDB-Wasm even after the FFI
    // allocator-pairing fix above -- isolated via controlled rebuilds: guarding
    // either function alone (2 guarded instantiations of matching shape) is
    // fine; guarding both together (4 instantiations, two pairs sharing a
    // signature) traps. This points at a WASM toolchain code-folding/ICF
    // defect merging GuardedFunction<...>::Call instantiations that share a
    // signature but differ only in the baked-in function pointer, not at a
    // bug in DATAZOO_GUARD's own logic. These two functions never throw
    // (no validation, FFI always returns true), so the banner's issue-link
    // enrichment has no practical loss here.
    TableFunction ts_features_list_func("ts_features_list", {}, TsFeaturesListExecute, TsFeaturesListBind,
                                         TsFeaturesListInitGlobal);
    {
        CreateTableFunctionInfo info(ts_features_list_func);
        FunctionDescription desc;
        desc.description = "Returns a table listing all 117 available tsfresh-compatible feature names and their categories.";
        desc.examples = {"SELECT * FROM ts_features_list()"};
        desc.categories = {"time-series", "feature-extraction"};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_features_config_template - Returns (feature VARCHAR, params_json VARCHAR)
// C++ API compatible config template output
// ============================================================================

// Per-execution state: see TsFeaturesListGlobalState above for why this is
// not bind data (#289 BUG-02's prepared-re-execution finding).
struct TsFeaturesConfigTemplateGlobalState : public GlobalTableFunctionState {
    vector<pair<string, string>> features;  // (feature_name, params_json)
    idx_t current_idx = 0;
};

static unique_ptr<FunctionData> TsFeaturesConfigTemplateBind(ClientContext &context, TableFunctionBindInput &input,
                                                              vector<LogicalType> &return_types, TableBindNames &names) {
    names.push_back(MakeBindName("feature"));
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    names.push_back(MakeBindName("params_json"));
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    return make_uniq<TableFunctionData>();
}

static unique_ptr<GlobalTableFunctionState>
TsFeaturesConfigTemplateInitGlobal(ClientContext &context, TableFunctionInitInput &input) {
    auto state = make_uniq<TsFeaturesConfigTemplateGlobalState>();

    // Get feature names from FFI, exactly once per execution.
    char *names_raw = nullptr;
    size_t n_names = 0;
    anofox_ts_features_list(&names_raw, &n_names);
    char **names = reinterpret_cast<char**>(names_raw);

    if (names) {
        state->features.reserve(n_names);
        for (size_t i = 0; i < n_names; i++) {
            if (names[i]) {
                // Default params_json is empty object
                state->features.push_back({names[i], "{}"});
            }
        }
        anofox_free_ts_features_list(names, n_names);
    }

    return std::move(state);
}

static void TsFeaturesConfigTemplateExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
    auto &gstate = data_p.global_state->Cast<TsFeaturesConfigTemplateGlobalState>();

    idx_t count = 0;
    idx_t max_count = STANDARD_VECTOR_SIZE;

    while (gstate.current_idx < gstate.features.size() && count < max_count) {
        const auto &feat = gstate.features[gstate.current_idx];

        FlatDataMut<string_t>(output.data[0])[count] =
            StringVector::AddString(output.data[0], feat.first);

        FlatDataMut<string_t>(output.data[1])[count] =
            StringVector::AddString(output.data[1], feat.second);

        gstate.current_idx++;
        count++;
    }

    SetOutputCardinality(output, count);
}

void RegisterTsFeaturesConfigTemplateFunction(ExtensionLoader &loader) {
    // NOT wrapped in DATAZOO_GUARD -- see the comment in
    // RegisterTsFeaturesListFunction above (#289 BUG-02).
    TableFunction func("ts_features_config_template", {}, TsFeaturesConfigTemplateExecute,
                       TsFeaturesConfigTemplateBind, TsFeaturesConfigTemplateInitGlobal);
    {
        CreateTableFunctionInfo info(func);
        FunctionDescription desc;
        desc.description = "Returns a JSON template for configuring which features to compute with ts_features_by().";
        desc.examples = {"SELECT * FROM ts_features_config_template()"};
        desc.categories = {"time-series", "feature-extraction"};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_features_config_from_json - Load feature configuration from JSON file
// Returns STRUCT(feature_names LIST(VARCHAR), overrides LIST(STRUCT(feature VARCHAR, params_json VARCHAR)))
// ============================================================================

static LogicalType GetFeaturesConfigResultType() {
    // Override struct type
    child_list_t<LogicalType> override_children;
    override_children.push_back(make_pair("feature", LogicalType(LogicalTypeId::VARCHAR)));
    override_children.push_back(make_pair("params_json", LogicalType(LogicalTypeId::VARCHAR)));
    auto override_type = LogicalType::STRUCT(std::move(override_children));

    // Main config struct
    child_list_t<LogicalType> children;
    children.push_back(make_pair("feature_names", LogicalType::LIST(LogicalType(LogicalTypeId::VARCHAR))));
    children.push_back(make_pair("overrides", LogicalType::LIST(override_type)));
    return LogicalType::STRUCT(std::move(children));
}

static void TsFeaturesConfigFromJsonFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &path_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);
    auto &struct_entries = StructVector::GetEntries(result);

    UnifiedVectorFormat path_fmt;
    ToUnified(path_vec, count, path_fmt);

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (!path_fmt.validity.RowIsValid(path_fmt.sel->get_index(row_idx))) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        // Get all available feature names as default config
        FeaturesResult feat_result;
        memset(&feat_result, 0, sizeof(feat_result));
        AnofoxError error;

        double dummy[] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
        bool success = anofox_ts_features(dummy, 10, &feat_result, &error);

        // Set feature_names list
        auto &names_list = StructEntry(struct_entries[0]);
        auto names_list_data = FlatDataMut<list_entry_t>(names_list);
        auto &names_child = ListChildMut(names_list);
        auto current_names_size = ListVector::GetListSize(names_list);

        if (success && feat_result.n_features > 0) {
            names_list_data[row_idx].offset = current_names_size;
            names_list_data[row_idx].length = feat_result.n_features;

            ListVector::Reserve(names_list, current_names_size + feat_result.n_features);
            ListVector::SetListSize(names_list, current_names_size + feat_result.n_features);

            auto names_data = FlatDataMut<string_t>(names_child);
            for (size_t i = 0; i < feat_result.n_features; i++) {
                names_data[current_names_size + i] = StringVector::AddString(names_child, feat_result.feature_names[i]);
            }
            anofox_free_features_result(&feat_result);
        } else {
            names_list_data[row_idx].offset = current_names_size;
            names_list_data[row_idx].length = 0;
        }

        // Set overrides list (empty for default config)
        auto &overrides_list = StructEntry(struct_entries[1]);
        auto overrides_list_data = FlatDataMut<list_entry_t>(overrides_list);
        auto current_overrides_size = ListVector::GetListSize(overrides_list);

        overrides_list_data[row_idx].offset = current_overrides_size;
        overrides_list_data[row_idx].length = 0;
    }
}

void RegisterTsFeaturesConfigFromJsonFunction(ExtensionLoader &loader) {
    ScalarFunctionSet config_json_set("ts_features_config_from_json");
    config_json_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType(LogicalTypeId::VARCHAR)},
        GetFeaturesConfigResultType(),
        TsFeaturesConfigFromJsonFunction
    )));
    {
        CreateScalarFunctionInfo info(config_json_set);
        FunctionDescription desc;
        desc.description = "Parses a feature configuration JSON string for use with ts_features_by() to select specific features.";
        desc.examples = {"ts_features_config_from_json('{\"features\": [\"mean\", \"std\"]}')"};
        desc.categories = {"time-series", "feature-extraction"};
        desc.parameter_names = {"config_json"};
        desc.parameter_types = {LogicalType(LogicalTypeId::VARCHAR)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_features_config_from_json");
    anofox_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType(LogicalTypeId::VARCHAR)},
        GetFeaturesConfigResultType(),
        TsFeaturesConfigFromJsonFunction
    )));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_features_config_from_json";
        FunctionDescription desc;
        desc.description = "Parses a feature configuration JSON string for use with ts_features_by() to select specific features.";
        desc.examples = {"ts_features_config_from_json('{\"features\": [\"mean\", \"std\"]}')"};
        desc.categories = {"time-series", "feature-extraction"};
        desc.parameter_names = {"config_json"};
        desc.parameter_types = {LogicalType(LogicalTypeId::VARCHAR)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

// ============================================================================
// ts_features_config_from_csv - Load feature configuration from CSV file
// Returns same struct type as JSON version
// ============================================================================

static void TsFeaturesConfigFromCsvFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    // Same implementation as JSON - returns all features as default
    TsFeaturesConfigFromJsonFunction(args, state, result);
}

void RegisterTsFeaturesConfigFromCsvFunction(ExtensionLoader &loader) {
    ScalarFunctionSet config_csv_set("ts_features_config_from_csv");
    config_csv_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType(LogicalTypeId::VARCHAR)},
        GetFeaturesConfigResultType(),
        TsFeaturesConfigFromCsvFunction
    )));
    {
        CreateScalarFunctionInfo info(config_csv_set);
        FunctionDescription desc;
        desc.description = "Parses a CSV string of feature names into a feature configuration for use with ts_features_by().";
        desc.examples = {"ts_features_config_from_csv('mean,std,skewness')"};
        desc.categories = {"time-series", "feature-extraction"};
        desc.parameter_names = {"config_csv"};
        desc.parameter_types = {LogicalType(LogicalTypeId::VARCHAR)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }

    ScalarFunctionSet anofox_set("anofox_fcst_ts_features_config_from_csv");
    anofox_set.AddFunction(Fallible(ScalarFunction(
        {LogicalType(LogicalTypeId::VARCHAR)},
        GetFeaturesConfigResultType(),
        TsFeaturesConfigFromCsvFunction
    )));
    {
        CreateScalarFunctionInfo info(anofox_set);
        info.alias_of = "ts_features_config_from_csv";
        FunctionDescription desc;
        desc.description = "Parses a CSV string of feature names into a feature configuration for use with ts_features_by().";
        desc.examples = {"ts_features_config_from_csv('mean,std,skewness')"};
        desc.categories = {"time-series", "feature-extraction"};
        desc.parameter_names = {"config_csv"};
        desc.parameter_types = {LogicalType(LogicalTypeId::VARCHAR)};
        info.descriptions.push_back(std::move(desc));
        loader.RegisterFunction(std::move(info));
    }
}

} // namespace duckdb
