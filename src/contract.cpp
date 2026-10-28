#include "contract.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/transaction/meta_transaction.hpp"
#include "duckdb_compat.hpp"

namespace duckdb {

// Same tagging pass as anofox-statistics' src/contract.cpp, so the family
// behaves alike whichever extension loads first.

static constexpr CatalogType FUNCTION_CATALOG_TYPES[] = {
    CatalogType::SCALAR_FUNCTION_ENTRY, CatalogType::AGGREGATE_FUNCTION_ENTRY, CatalogType::TABLE_FUNCTION_ENTRY,
    CatalogType::MACRO_ENTRY, CatalogType::TABLE_MACRO_ENTRY};

// Contract producers: function name -> output schema. A function is listed only
// once its output carries every required column of its schema;
// test/sql/correctness/contract.test checks each one. Not (yet) listed: the
// conformal outputs (their date column is literally `date_col`, no ds/x/period),
// the wide one-metric macros (ts_metrics_by is their summary form) and the
// list-returning producers (their *_long_by companions are listed instead).
// clang-format off
static const struct {
    const char *name;
    const char *output;
} CONTRACT_PRODUCERS[] = {
    {"ts_forecast_by", "prediction"}, {"ts_forecast_exog_by", "prediction"},
    {"ts_forecast_ensemble_by", "prediction"}, {"ts_forecast_panel_by", "prediction"},
    {"ts_forecast_var_by", "prediction"}, {"ts_cv_forecast_by", "prediction"},
    {"ts_adf_by", "test"}, {"ts_kpss_by", "test"}, {"ts_ljung_box_by", "test"},
    {"ts_jarque_bera_by", "test"}, {"ts_durbin_watson_by", "test"},
    {"ts_metrics_by", "summary"},
    {"ts_detect_changepoints_events_by", "events"}, {"ts_detect_peaks_long_by", "events"},
    {"ts_mstl_decomposition_long_by", "decomposition"}, {"ts_detrend_long_by", "decomposition"},
    {"ts_forecast_explain_long_by", "decomposition"},
    {"ts_forecast_fitted_by", "obs"},
    {"ts_cv_folds_by", "cv"},
    {nullptr, nullptr}
};
// clang-format on

static SchemaCatalogEntry &SystemMainSchema(ExtensionLoader &loader) {
    auto &db = loader.GetDatabaseInstance();
    return Catalog::GetSystemCatalog(db).GetSchema(CatalogTransaction::GetSystemTransaction(db), DEFAULT_SCHEMA);
}

case_insensitive_set_t SnapshotSystemFunctionNames(ExtensionLoader &loader) {
    case_insensitive_set_t names;
    auto &schema = SystemMainSchema(loader);
    for (auto type : FUNCTION_CATALOG_TYPES) {
        schema.Scan(type, [&](CatalogEntry &entry) { names.insert(NameOf(entry)); });
    }
    return names;
}

static void ContractVersionFunction(DataChunk &args, ExpressionState &state, Vector &result) {
    result.SetVectorType(VectorType::CONSTANT_VECTOR);
    ConstantVector::GetData<string_t>(result)[0] = StringVector::AddString(result, ANOFOX_CONTRACT_VERSION);
}

void RegisterContractVersionFunction(ExtensionLoader &loader, const case_insensitive_set_t &preexisting) {
    if (preexisting.find("anofox_contract_version") != preexisting.end()) {
        return;
    }
    ScalarFunction func("anofox_contract_version", {}, LogicalType::VARCHAR, ContractVersionFunction);
    CreateScalarFunctionInfo info(func);
    FunctionDescription desc;
    desc.description = "Returns the version of the anofox integration contract (output schemas and "
                       "duckdb_functions() tags) this extension implements.";
    desc.examples = {"anofox_contract_version()"};
    desc.categories = {"metadata"};
    info.descriptions.push_back(std::move(desc));
    loader.RegisterFunction(std::move(info));
}

void ApplyContractTags(ExtensionLoader &loader, const case_insensitive_set_t &preexisting) {
    case_insensitive_map_t<string> outputs;
    for (idx_t i = 0; CONTRACT_PRODUCERS[i].name != nullptr; i++) {
        outputs[CONTRACT_PRODUCERS[i].name] = CONTRACT_PRODUCERS[i].output;
    }
    auto &schema = SystemMainSchema(loader);
    for (auto type : FUNCTION_CATALOG_TYPES) {
        schema.Scan(type, [&](CatalogEntry &entry) {
            // anofox_contract_version() is shared by the whole anofox family;
            // whichever extension loads first registers it, untagged.
            if (preexisting.find(NameOf(entry)) != preexisting.end() || NameOf(entry) == "anofox_contract_version") {
                return;
            }
            entry.tags["anofox.family"] = "forecast";
            auto output = outputs.find(NameOf(entry));
            if (output != outputs.end()) {
                entry.tags["anofox.contract"] = ANOFOX_CONTRACT_VERSION;
                entry.tags["anofox.output"] = output->second;
            }
        });
    }
}

} // namespace duckdb
