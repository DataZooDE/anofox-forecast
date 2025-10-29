#define DUCKDB_EXTENSION_MAIN

#include "anofox_forecast_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
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
#include "duckdb/catalog/default/default_functions.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"

// OpenSSL linked through vcpkg
#include <openssl/opensslv.h>

namespace duckdb {

// Table macros - user-facing API with automatic UNNEST
// New signature: TS_FORECAST_BY(table_name, group_by_columns, date_col, target_col, method, horizon, params)
// Users get direct table output, no manual UNNEST needed!
static const DefaultTableMacro forecast_table_macros[] = {
    // TS_FORECAST: Single series (no GROUP BY)
    {DEFAULT_SCHEMA,
     "ts_forecast",
     {"table_name", "date_col", "target_col", "method", "horizon", "params", nullptr},
     {{nullptr, nullptr}},
     R"(
        WITH fc AS (
            SELECT TS_FORECAST_AGG(date_col, target_col, method, horizon, params) AS result
            FROM QUERY_TABLE(table_name)
        )
        SELECT 
            UNNEST(result.forecast_step) AS forecast_step,
            UNNEST(result.forecast_timestamp) AS date_col,
            UNNEST(result.point_forecast) AS point_forecast,
            UNNEST(result.lower) AS lower,
            UNNEST(result.upper) AS upper,
            result.model_name AS model_name,
            result.insample_fitted AS insample_fitted,
            result.confidence_level AS confidence_level
        FROM fc
    )"},
    // TS_FORECAST_BY: Multiple series (1 group column)
    {DEFAULT_SCHEMA,
     "ts_forecast_by",
     {"table_name", "group_col", "date_col", "target_col", "method", "horizon", "params", nullptr},
     {{nullptr, nullptr}},
     R"(
        WITH fc AS (
            SELECT 
                group_col,
                TS_FORECAST_AGG(date_col, target_col, method, horizon, params) AS result
            FROM QUERY_TABLE(table_name)
            GROUP BY group_col
        )
        SELECT 
            group_col,
            UNNEST(result.forecast_step) AS forecast_step,
            UNNEST(result.forecast_timestamp) AS date_col,
            UNNEST(result.point_forecast) AS point_forecast,
            UNNEST(result.lower) AS lower,
            UNNEST(result.upper) AS upper,
            result.model_name AS model_name,
            result.insample_fitted AS insample_fitted,
            result.confidence_level AS confidence_level
        FROM fc
    )"},
    // TS_FORECAST_BY_MULTI: Multiple series (multiple group columns via COLUMNS)
    // Pass group columns as pipe-separated string: 'col1|col2|col3'
    {DEFAULT_SCHEMA,
     "ts_forecast_by_multi",
     {"table_name", "group_cols", "date_col", "target_col", "method", "horizon", "params", nullptr},
     {{nullptr, nullptr}},
     R"(
        WITH fc AS (
            SELECT 
                COLUMNS(group_cols),
                TS_FORECAST_AGG(date_col, target_col, method, horizon, params) AS result
            FROM QUERY_TABLE(table_name)
            GROUP BY ALL
        )
        SELECT 
            COLUMNS(group_cols),
            UNNEST(result.forecast_step) AS forecast_step,
            UNNEST(result.forecast_timestamp) AS date_col,
            UNNEST(result.point_forecast) AS point_forecast,
            UNNEST(result.lower) AS lower,
            UNNEST(result.upper) AS upper,
            result.model_name AS model_name,
            result.insample_fitted AS insample_fitted,
            result.confidence_level AS confidence_level
        FROM fc
    )"},
    // TS_DETECT_CHANGEPOINTS: Single series (no GROUP BY)
    {DEFAULT_SCHEMA,
     "ts_detect_changepoints",
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
            row_data.is_changepoint AS is_changepoint,
            row_data.changepoint_probability AS changepoint_probability
        FROM unnested
    )"},
    // TS_DETECT_CHANGEPOINTS_BY: Multiple series (1 group column)
    {DEFAULT_SCHEMA,
     "ts_detect_changepoints_by",
     {"table_name", "group_col", "date_col", "value_col", "params", nullptr},
     {{nullptr, nullptr}},
     R"(
        WITH cp AS (
            SELECT 
                group_col,
                TS_DETECT_CHANGEPOINTS_AGG(date_col, value_col, params) AS result
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
            row_data.is_changepoint AS is_changepoint,
            row_data.changepoint_probability AS changepoint_probability
        FROM unnested
    )"},
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

static void LoadInternal(ExtensionLoader &loader) {
	// std::cerr << "[DEBUG] Loading anofox_forecast extension..." << std::endl;

	// Register the FORECAST table function (legacy, for compatibility)
	auto forecast_function = CreateForecastTableFunction();
	loader.RegisterFunction(*forecast_function);
	// std::cerr << "[DEBUG] FORECAST table function registered" << std::endl;

	// Register the TS_FORECAST_AGG aggregate function (internal, for GROUP BY)
	auto ts_forecast_agg = CreateTSForecastAggregate();
	loader.RegisterFunction(ts_forecast_agg);
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

	// Register table macros (TS_FORECAST, TS_FORECAST_BY)
	// Both handle UNNEST internally - users get clean table output!
	// For 2+ group columns, use TS_FORECAST_AGG with manual UNNEST
	for (idx_t index = 0; forecast_table_macros[index].name != nullptr; index++) {
		auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(forecast_table_macros[index]);
		loader.RegisterFunction(*table_info);
	}
	// std::cerr << "[DEBUG] TS_FORECAST table macros registered" << std::endl;

	// Register EDA (Exploratory Data Analysis) macros
	RegisterEDAMacros(loader);
	// std::cerr << "[DEBUG] EDA macros registered" << std::endl;

	// Register Data Preparation macros
	RegisterDataPrepMacros(loader);
	// std::cerr << "[DEBUG] Data Preparation macros registered" << std::endl;

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
