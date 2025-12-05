#define DUCKDB_EXTENSION_MAIN

#include "anofox_forecast_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parsed_data/create_aggregate_function_info.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"

// Include our forecast functions
#include "forecast_table_function.hpp"
#include "forecast_aggregate.hpp"
#include "metrics_function.hpp"
#include "seasonality_function.hpp"
#include "changepoint_function.hpp"
#include "eda_macros.hpp"
#include "data_prep_macros.hpp"
#include "mstl_decomposition_function.hpp"
#include "ts_features_function.hpp"
#include "duckdb/catalog/default/default_functions.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"

// OpenSSL linked through vcpkg
// #include <openssl/opensslv.h>  // Not currently used

namespace duckdb {

static void RegisterTableFunctionIgnore(ExtensionLoader &loader, TableFunction function) {
	TableFunctionSet set(function.name);
	set.AddFunction(std::move(function));
	CreateTableFunctionInfo info(std::move(set));
	info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(info));
}

static void RegisterAggregateFunctionIgnore(ExtensionLoader &loader, AggregateFunction function) {
	AggregateFunctionSet set(function.name);
	set.AddFunction(std::move(function));
	CreateAggregateFunctionInfo info(std::move(set));
	info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(info));
}

static void RegisterAggregateFunctionWithAlias(ExtensionLoader &loader, AggregateFunction function,
                                               const string &alias_name) {
	// Register main function
	AggregateFunctionSet main_set(function.name);
	main_set.AddFunction(function);
	CreateAggregateFunctionInfo main_info(std::move(main_set));
	main_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(main_info));

	// Register alias
	AggregateFunction alias_function = function;
	alias_function.name = alias_name;
	AggregateFunctionSet alias_set(alias_name);
	alias_set.AddFunction(std::move(alias_function));
	CreateAggregateFunctionInfo alias_info(std::move(alias_set));
	alias_info.alias_of = function.name;
	alias_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(alias_info));
}

static void RegisterScalarFunctionWithAlias(ExtensionLoader &loader, ScalarFunction function,
                                            const string &alias_name) {
	// Register main function
	ScalarFunctionSet main_set(function.name);
	main_set.AddFunction(function);
	CreateScalarFunctionInfo main_info(std::move(main_set));
	main_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(main_info));

	// Register alias
	ScalarFunction alias_function = function;
	alias_function.name = alias_name;
	ScalarFunctionSet alias_set(alias_name);
	alias_set.AddFunction(std::move(alias_function));
	CreateScalarFunctionInfo alias_info(std::move(alias_set));
	alias_info.alias_of = function.name;
	alias_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(alias_info));
}

// Table macros - user-facing API with automatic UNNEST
// New signature: TS_FORECAST_BY(table_name, group_by_columns, date_col, target_col, method, horizon, params)
// Users get direct table output, no manual UNNEST needed!
static const DefaultTableMacro forecast_table_macros[] = {
    // TS_FORECAST: Single series (no GROUP BY)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_forecast",
     {"table_name", "date_col", "target_col", "method", "horizon", "params", nullptr},
     {{nullptr, nullptr}},
     R"(
        WITH fc AS (
            SELECT anofox_fcst_ts_forecast_agg(date_col, target_col, method, horizon, params) AS result
            FROM QUERY_TABLE(table_name)
        ),
        expanded AS (
            SELECT result.* FROM fc
        )
        SELECT 
            UNNEST(forecast_step) AS forecast_step,
            UNNEST(forecast_timestamp) AS date,
            UNNEST(point_forecast) AS point_forecast,
            UNNEST(COLUMNS(c -> c LIKE 'lower_%')),
            UNNEST(COLUMNS(c -> c LIKE 'upper_%')),
            model_name,
            insample_fitted
        FROM expanded
    )"},
    // TS_FORECAST_BY: Multiple series (1 group column)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_forecast_by",
     {"table_name", "group_col", "date_col", "target_col", "method", "horizon", "params", nullptr},
     {{nullptr, nullptr}},
     R"(
        WITH fc AS (
            SELECT 
                group_col,
                TS_FORECAST_AGG(date_col, target_col, method, horizon, params) AS result
            FROM QUERY_TABLE(table_name)
            GROUP BY group_col
        ),
        expanded AS (
            SELECT group_col, result.* FROM fc
        )
        SELECT 
            group_col,
            UNNEST(forecast_step) AS forecast_step,
            UNNEST(forecast_timestamp) AS date,
            UNNEST(point_forecast) AS point_forecast,
            UNNEST(COLUMNS(c -> c LIKE 'lower_%')),
            UNNEST(COLUMNS(c -> c LIKE 'upper_%')),
            model_name,
            insample_fitted
        FROM expanded
    )"},
    // TS_DETECT_CHANGEPOINTS: Single series (no GROUP BY)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_detect_changepoints",
     {"table_name", "date_col", "value_col", "params", nullptr},
     {{nullptr, nullptr}},
     R"(
        WITH cp AS (
            SELECT TS_DETECT_CHANGEPOINTS_AGG(date_col, value_col, params) AS result
            FROM QUERY_TABLE(table_name)
        ),
        unnested AS (
            SELECT UNNEST(result) AS row_data
            FROM cp
        )
        SELECT 
            row_data.timestamp AS date_col,
            row_data.value AS value_col,
            row_data.is_changepoint,
            row_data.changepoint_probability
        FROM unnested
    )"},
    // TS_DETECT_CHANGEPOINTS_BY: Multiple series (1 group column)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_detect_changepoints_by",
     {"table_name", "group_col", "date_col", "value_col", "params", nullptr},
     {{nullptr, nullptr}},
     R"(
        WITH cp AS (
            SELECT 
                group_col,
                anofox_fcst_ts_detect_changepoints_agg(date_col, value_col, params) AS result
            FROM QUERY_TABLE(table_name)
            GROUP BY group_col
        ),
        unnested AS (
            SELECT 
                group_col,
                UNNEST(result) AS row_data
            FROM cp
        )
        SELECT 
            group_col,
            row_data.timestamp AS date_col,
            row_data.value AS value_col,
            row_data.is_changepoint,
            row_data.changepoint_probability
        FROM unnested
    )"},
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

static void LoadInternal(ExtensionLoader &loader) {
	// std::cerr << "[DEBUG] Loading anofox_forecast extension..." << std::endl;

	// Register the FORECAST table function (legacy, for compatibility)
	auto forecast_function = CreateForecastTableFunction();
	// Register main function
	RegisterTableFunctionIgnore(loader, std::move(*forecast_function));
	// Register alias
	auto alias_function = CreateForecastTableFunction();
	(*alias_function).name = "forecast";
	TableFunctionSet alias_set("forecast");
	alias_set.AddFunction(*alias_function);
	CreateTableFunctionInfo alias_info(std::move(alias_set));
	alias_info.alias_of = "anofox_fcst_forecast";
	alias_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(alias_info));
	// std::cerr << "[DEBUG] FORECAST table function registered" << std::endl;

	// Register the TS_FORECAST_AGG aggregate function (internal, for GROUP BY)
	auto ts_forecast_agg = CreateTSForecastAggregate();
	RegisterAggregateFunctionWithAlias(loader, std::move(ts_forecast_agg), "ts_forecast_agg");
	// std::cerr << "[DEBUG] TS_FORECAST_AGG aggregate function registered" << std::endl;

	// Register the TS_METRICS scalar functions (for evaluation)
	RegisterMetricsFunction(loader);
	// std::cerr << "[DEBUG] TS_METRICS functions registered" << std::endl;

	// Register seasonality detection functions
	RegisterSeasonalityFunction(loader);
	// std::cerr << "[DEBUG] Seasonality functions registered" << std::endl;

	// Register changepoint detection functions
	RegisterChangepointFunction(loader);
	// std::cerr << "[DEBUG] Changepoint functions registered" << std::endl;

	// Register ts_features aggregate/window function
	RegisterTSFeaturesFunction(loader);

	// Register table macros (TS_FORECAST, TS_FORECAST_BY)
	// Both handle UNNEST internally - users get clean table output!
	// For 2+ group columns, use TS_FORECAST_AGG with manual UNNEST
	for (idx_t index = 0; forecast_table_macros[index].name != nullptr; index++) {
		auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(forecast_table_macros[index]);
		table_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
		loader.RegisterFunction(*table_info);

		// Register alias (without anofox_fcst_ prefix)
		if (forecast_table_macros[index].name && string(forecast_table_macros[index].name).find("anofox_fcst_") == 0) {
			string alias_name = string(forecast_table_macros[index].name).substr(12); // Remove "anofox_fcst_" prefix
			DefaultTableMacro alias_macro = forecast_table_macros[index];
			alias_macro.name = alias_name.c_str();
			auto alias_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(alias_macro);
			alias_info->alias_of = forecast_table_macros[index].name;
			alias_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
			loader.RegisterFunction(*alias_info);
		}
	}
	// std::cerr << "[DEBUG] TS_FORECAST table macros registered" << std::endl;

	// Register EDA (Exploratory Data Analysis) table functions (bind_replace)
	RegisterEDATableFunctions(loader);

	// Register EDA (Exploratory Data Analysis) macros (for functions not converted to table functions)
	RegisterEDAMacros(loader);
	// std::cerr << "[DEBUG] EDA macros registered" << std::endl;

	// Register Data Preparation macros
	RegisterDataPrepMacros(loader);
	// std::cerr << "[DEBUG] Data Preparation macros registered" << std::endl;

	// Register Data Quality macros
	RegisterDataQualityMacros(loader);
	// std::cerr << "[DEBUG] Data Quality macros registered" << std::endl;

	// Register MSTL decomposition
	RegisterMstlDecompositionFunctions(loader);


	// std::cerr << "[DEBUG] All functions registered successfully" << std::endl;
}

void AnofoxForecastExtension::Load(ExtensionLoader &loader) {
	// std::cerr << "[DEBUG] AnofoxForecastExtension::Load called" << std::endl;
	LoadInternal(loader);
}

std::string AnofoxForecastExtension::Name() {
	return "anofox_forecast";
}

std::string AnofoxForecastExtension::Version() const {
#ifdef EXT_VERSION_ANOFOX_FORECAST
	return EXT_VERSION_ANOFOX_FORECAST;
#else
	return "1.0.0";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(anofox_forecast, loader) {
	// std::cerr << "[DEBUG] C extension entry point called" << std::endl;
	duckdb::LoadInternal(loader);
}
}
