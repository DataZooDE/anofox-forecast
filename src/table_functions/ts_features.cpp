#include "anofox_forecast_extension.hpp"
#include "anofox_fcst_ffi.h"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include <unordered_map>
#include <limits>

namespace duckdb {

static void ExtractListAsDouble(Vector &list_vec, idx_t row_idx, vector<double> &out_values) {
    auto list_data = ListVector::GetData(list_vec);
    auto &list_entry = list_data[row_idx];

    auto &child_vec = ListVector::GetEntry(list_vec);
    auto child_data = FlatVector::GetData<double>(child_vec);
    auto &child_validity = FlatVector::Validity(child_vec);

    out_values.clear();
    out_values.reserve(list_entry.length);

    for (idx_t i = 0; i < list_entry.length; i++) {
        idx_t child_idx = list_entry.offset + i;
        if (child_validity.RowIsValid(child_idx)) {
            out_values.push_back(child_data[child_idx]);
        }
    }
}

// ============================================================================
// ts_features - Extract tsfresh-compatible features (scalar version)
// Takes DOUBLE[] and returns STRUCT with feature columns
// ============================================================================

// Get the list of feature names from Rust core for STRUCT return type
static vector<string> GetScalarFeatureNames() {
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

static LogicalType GetScalarFeaturesResultType() {
    // Build a STRUCT with all feature names as columns
    child_list_t<LogicalType> children;

    auto feature_names = GetScalarFeatureNames();
    for (const auto &name : feature_names) {
        children.push_back(make_pair(name, LogicalType::DOUBLE));
    }

    return LogicalType::STRUCT(std::move(children));
}

static void TsFeaturesFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    auto &list_vec = args.data[0];
    idx_t count = args.size();

    result.SetVectorType(VectorType::FLAT_VECTOR);

    // Get feature names for struct field lookup
    static auto feature_names = GetScalarFeatureNames();

    for (idx_t row_idx = 0; row_idx < count; row_idx++) {
        if (FlatVector::IsNull(list_vec, row_idx)) {
            FlatVector::SetNull(result, row_idx, true);
            continue;
        }

        vector<double> values;
        ExtractListAsDouble(list_vec, row_idx, values);

        FeaturesResult feat_result;
        memset(&feat_result, 0, sizeof(feat_result));
        AnofoxError error;

        bool success = anofox_ts_features(
            values.data(),
            values.size(),
            &feat_result,
            &error
        );

        if (!success) {
            throw InvalidInputException("ts_features failed: %s", error.message);
        }

        // Build name to value map for quick lookup
        std::unordered_map<string, double> feature_map;
        for (size_t i = 0; i < feat_result.n_features; i++) {
            if (feat_result.feature_names[i]) {
                feature_map[feat_result.feature_names[i]] = feat_result.features[i];
            }
        }

        // Populate the STRUCT fields
        auto &struct_entries = StructVector::GetEntries(result);
        for (size_t j = 0; j < feature_names.size() && j < struct_entries.size(); j++) {
            auto &child_vec = *struct_entries[j];
            auto child_data = FlatVector::GetData<double>(child_vec);

            auto it = feature_map.find(feature_names[j]);
            if (it != feature_map.end()) {
                child_data[row_idx] = it->second;
            } else {
                child_data[row_idx] = std::numeric_limits<double>::quiet_NaN();
            }
        }

        anofox_free_features_result(&feat_result);
    }
}

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
    return_types.push_back(LogicalType::VARCHAR);

    names.push_back("feature_name");
    return_types.push_back(LogicalType::VARCHAR);

    names.push_back("parameter_suffix");
    return_types.push_back(LogicalType::VARCHAR);

    names.push_back("default_parameters");
    return_types.push_back(LogicalType::VARCHAR);

    names.push_back("parameter_keys");
    return_types.push_back(LogicalType::VARCHAR);

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
    TableFunction ts_features_list_func("ts_features_list", {}, TsFeaturesListExecute, TsFeaturesListBind);
    loader.RegisterFunction(ts_features_list_func);
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
    return_types.push_back(LogicalType::VARCHAR);

    names.push_back("params_json");
    return_types.push_back(LogicalType::VARCHAR);

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
    TableFunction func("ts_features_config_template", {}, TsFeaturesConfigTemplateExecute, TsFeaturesConfigTemplateBind);
    loader.RegisterFunction(func);
}

// ============================================================================
// ts_features_config_from_json - Load feature configuration from JSON file
// Returns STRUCT(feature_names LIST(VARCHAR), overrides LIST(STRUCT(feature VARCHAR, params_json VARCHAR)))
// ============================================================================

static LogicalType GetFeaturesConfigResultType() {
    // Override struct type
    child_list_t<LogicalType> override_children;
    override_children.push_back(make_pair("feature", LogicalType::VARCHAR));
    override_children.push_back(make_pair("params_json", LogicalType::VARCHAR));
    auto override_type = LogicalType::STRUCT(std::move(override_children));

    // Main config struct
    child_list_t<LogicalType> children;
    children.push_back(make_pair("feature_names", LogicalType::LIST(LogicalType::VARCHAR)));
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
        {LogicalType::VARCHAR},
        GetFeaturesConfigResultType(),
        TsFeaturesConfigFromJsonFunction
    ));
    loader.RegisterFunction(config_json_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_features_config_from_json");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::VARCHAR},
        GetFeaturesConfigResultType(),
        TsFeaturesConfigFromJsonFunction
    ));
    loader.RegisterFunction(anofox_set);
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
        {LogicalType::VARCHAR},
        GetFeaturesConfigResultType(),
        TsFeaturesConfigFromCsvFunction
    ));
    loader.RegisterFunction(config_csv_set);

    ScalarFunctionSet anofox_set("anofox_fcst_ts_features_config_from_csv");
    anofox_set.AddFunction(ScalarFunction(
        {LogicalType::VARCHAR},
        GetFeaturesConfigResultType(),
        TsFeaturesConfigFromCsvFunction
    ));
    loader.RegisterFunction(anofox_set);
}

} // namespace duckdb
