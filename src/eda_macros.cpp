#include "duckdb.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"

namespace duckdb {

// Array of EDA macros - all follow consistent signature:
// (table_name, group_col, date_col, value_col)
static const DefaultTableMacro eda_macros[] = {

    // TS_STATS: Per-series comprehensive statistics
    {DEFAULT_SCHEMA,
     "ts_stats",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            SELECT 
                group_col AS series_id,
                COUNT(*) AS length,
                MIN(date_col) AS start_date,
                MAX(date_col) AS end_date,
                ROUND(AVG(value_col), 2) AS mean,
                ROUND(STDDEV(value_col), 2) AS std,
                ROUND(MIN(value_col), 2) AS min,
                ROUND(MAX(value_col), 2) AS max,
                ROUND(MEDIAN(value_col), 2) AS median,
                COUNT(CASE WHEN value_col IS NULL THEN 1 END) AS n_null,
                COUNT(CASE WHEN value_col = 0 THEN 1 END) AS n_zeros,
                COUNT(DISTINCT value_col) AS n_unique,
                COUNT(DISTINCT value_col) = 1 AS is_constant
            FROM QUERY_TABLE(table_name)
            GROUP BY group_col
            ORDER BY group_col
        )"},

    // TS_DETECT_SEASONALITY_ALL: Detect seasonality for all series
    {DEFAULT_SCHEMA,
     "ts_detect_seasonality_all",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_agg AS (
                SELECT 
                    group_col AS series_id,
                    LIST(value_col ORDER BY date_col) AS values
                FROM QUERY_TABLE(table_name)
                GROUP BY group_col
            )
            SELECT 
                series_id,
                TS_DETECT_SEASONALITY(values) AS detected_periods,
                CASE 
                    WHEN LEN(TS_DETECT_SEASONALITY(values)) > 0 
                    THEN TS_DETECT_SEASONALITY(values)[1]
                    ELSE NULL 
                END AS primary_period,
                LEN(TS_DETECT_SEASONALITY(values)) > 0 AS is_seasonal
            FROM series_agg
        )"},

    // End marker
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

// Register EDA (Exploratory Data Analysis) table macros
void RegisterEDAMacros(ExtensionLoader &loader) {
	for (idx_t index = 0; eda_macros[index].name != nullptr; index++) {
		auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(eda_macros[index]);
		table_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
		loader.RegisterFunction(*table_info);
	}
}

} // namespace duckdb
