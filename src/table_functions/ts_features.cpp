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
// was never reachable from SQL: RegisterTsFeaturesFunction below is a no-op
// (C++ extension exposes ts_features only as the aggregate registered in
// ts_features_agg.cpp). Deleted per Phase 12 D-01/D-11 plan 12-03 — its
// private ExtractListAsDouble/GetScalarFeatureNames/GetScalarFeaturesResultType
// helpers were used only by this dead body. RegisterTsFeaturesFunction itself
// is kept as a no-op (plan 12-07 removes no-op registrations).
// ============================================================================

void RegisterTsFeaturesFunction(ExtensionLoader &loader) {
    // No-op: C++ extension only has ts_features as aggregate function
    // The aggregate function is registered in ts_features_agg.cpp
}

// ============================================================================
// ts_features_list - List available features as TABLE
// C++ API compatible: Returns TABLE(column_name, feature_name, parameter_suffix, default_parameters, parameter_keys)
// ============================================================================

struct TsFeaturesListData : public TableFunctionData {
    vector<string> feature_names;
    idx_t current_idx = 0;
    bool initialized = false;
};

static unique_ptr<FunctionData> TsFeaturesListBind(ClientContext &context, TableFunctionBindInput &input,
                                                    vector<LogicalType> &return_types, vector<string> &names) {
    // Define return columns to match C++ extension
    names.push_back("column_name");
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    names.push_back("feature_name");
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    names.push_back("parameter_suffix");
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    names.push_back("default_parameters");
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    names.push_back("parameter_keys");
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    return make_uniq<TsFeaturesListData>();
}

static void TsFeaturesListExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
    auto &data = data_p.bind_data->CastNoConst<TsFeaturesListData>();

    if (!data.initialized) {
        // Get feature names from FFI
        char *names_raw = nullptr;
        size_t n_names = 0;
        anofox_ts_features_list(&names_raw, &n_names);
        char **names = reinterpret_cast<char**>(names_raw);

        if (names) {
            for (size_t i = 0; i < n_names; i++) {
                data.feature_names.push_back(names[i]);
                free(names[i]);
            }
            free(names);
        }
        data.initialized = true;
    }

    idx_t count = 0;
    idx_t max_count = STANDARD_VECTOR_SIZE;

    while (data.current_idx < data.feature_names.size() && count < max_count) {
        const auto &name = data.feature_names[data.current_idx];

        // column_name: "value" (default column name for features)
        FlatVector::GetData<string_t>(output.data[0])[count] =
            StringVector::AddString(output.data[0], "value");

        // feature_name: the actual feature name
        FlatVector::GetData<string_t>(output.data[1])[count] =
            StringVector::AddString(output.data[1], name);

        // parameter_suffix: empty for basic features
        FlatVector::GetData<string_t>(output.data[2])[count] =
            StringVector::AddString(output.data[2], "");

        // default_parameters: empty for basic features
        FlatVector::GetData<string_t>(output.data[3])[count] =
            StringVector::AddString(output.data[3], "{}");

        // parameter_keys: empty for basic features
        FlatVector::GetData<string_t>(output.data[4])[count] =
            StringVector::AddString(output.data[4], "");

        data.current_idx++;
        count++;
    }

    output.SetCardinality(count);
}

void RegisterTsFeaturesListFunction(ExtensionLoader &loader) {
    TableFunction ts_features_list_func("ts_features_list", {}, DATAZOO_GUARD(ANOFOX_FORECAST_BANNER, TsFeaturesListExecute), DATAZOO_GUARD(ANOFOX_FORECAST_BANNER, TsFeaturesListBind));
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

struct TsFeaturesConfigTemplateData : public TableFunctionData {
    vector<pair<string, string>> features;  // (feature_name, params_json)
    idx_t current_idx = 0;
    bool initialized = false;
};

static unique_ptr<FunctionData> TsFeaturesConfigTemplateBind(ClientContext &context, TableFunctionBindInput &input,
                                                              vector<LogicalType> &return_types, vector<string> &names) {
    names.push_back("feature");
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    names.push_back("params_json");
    return_types.push_back(LogicalType(LogicalTypeId::VARCHAR));

    return make_uniq<TsFeaturesConfigTemplateData>();
}

static void TsFeaturesConfigTemplateExecute(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
    auto &data = data_p.bind_data->CastNoConst<TsFeaturesConfigTemplateData>();

    if (!data.initialized) {
        // Get feature names from FFI
        char *names_raw = nullptr;
        size_t n_names = 0;
        anofox_ts_features_list(&names_raw, &n_names);
        char **names = reinterpret_cast<char**>(names_raw);

        if (names) {
            for (size_t i = 0; i < n_names; i++) {
                // Default params_json is empty object
                data.features.push_back({names[i], "{}"});
                free(names[i]);
            }
            free(names);
        }
        data.initialized = true;
    }

    idx_t count = 0;
    idx_t max_count = STANDARD_VECTOR_SIZE;

    while (data.current_idx < data.features.size() && count < max_count) {
        const auto &feat = data.features[data.current_idx];

        FlatVector::GetData<string_t>(output.data[0])[count] =
            StringVector::AddString(output.data[0], feat.first);

        FlatVector::GetData<string_t>(output.data[1])[count] =
            StringVector::AddString(output.data[1], feat.second);

        data.current_idx++;
        count++;
    }

    output.SetCardinality(count);
}

void RegisterTsFeaturesConfigTemplateFunction(ExtensionLoader &loader) {
    TableFunction func("ts_features_config_template", {}, DATAZOO_GUARD(ANOFOX_FORECAST_BANNER, TsFeaturesConfigTemplateExecute), DATAZOO_GUARD(ANOFOX_FORECAST_BANNER, TsFeaturesConfigTemplateBind));
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

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(path_vec, row_idx)) {
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
        auto &names_list = *struct_entries[0];
        auto names_list_data = FlatVector::GetData<list_entry_t>(names_list);
        auto &names_child = ListVector::GetEntry(names_list);
        auto current_names_size = ListVector::GetListSize(names_list);

        if (success && feat_result.n_features > 0) {
            names_list_data[row_idx].offset = current_names_size;
            names_list_data[row_idx].length = feat_result.n_features;

            ListVector::Reserve(names_list, current_names_size + feat_result.n_features);
            ListVector::SetListSize(names_list, current_names_size + feat_result.n_features);

            auto names_data = FlatVector::GetData<string_t>(names_child);
            for (size_t i = 0; i < feat_result.n_features; i++) {
                names_data[current_names_size + i] = StringVector::AddString(names_child, feat_result.feature_names[i]);
            }
            anofox_free_features_result(&feat_result);
        } else {
            names_list_data[row_idx].offset = current_names_size;
            names_list_data[row_idx].length = 0;
        }

        // Set overrides list (empty for default config)
        auto &overrides_list = *struct_entries[1];
        auto overrides_list_data = FlatVector::GetData<list_entry_t>(overrides_list);
        auto current_overrides_size = ListVector::GetListSize(overrides_list);

        overrides_list_data[row_idx].offset = current_overrides_size;
        overrides_list_data[row_idx].length = 0;
    }
}

void RegisterTsFeaturesConfigFromJsonFunction(ExtensionLoader &loader) {
    ScalarFunctionSet config_json_set("ts_features_config_from_json");
    config_json_set.AddFunction(ScalarFunction(
        {LogicalType(LogicalTypeId::VARCHAR)},
        GetFeaturesConfigResultType(),
        TsFeaturesConfigFromJsonFunction
    ));
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
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType(LogicalTypeId::VARCHAR)},
        GetFeaturesConfigResultType(),
        TsFeaturesConfigFromJsonFunction
    ));
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
    config_csv_set.AddFunction(ScalarFunction(
        {LogicalType(LogicalTypeId::VARCHAR)},
        GetFeaturesConfigResultType(),
        TsFeaturesConfigFromCsvFunction
    ));
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
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType(LogicalTypeId::VARCHAR)},
        GetFeaturesConfigResultType(),
        TsFeaturesConfigFromCsvFunction
    ));
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
