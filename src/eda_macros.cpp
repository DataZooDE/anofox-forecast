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
            WITH ordered_data AS (
                SELECT 
                    group_col,
                    date_col,
                    value_col,
                    ROW_NUMBER() OVER (PARTITION BY group_col ORDER BY date_col) AS row_num
                FROM QUERY_TABLE(table_name)
            ),
            value_changes AS (
                SELECT 
                    group_col,
                    date_col,
                    value_col,
                    row_num,
                    CASE 
                        WHEN value_col IS DISTINCT FROM LAG(value_col) OVER (PARTITION BY group_col ORDER BY date_col)
                        THEN 1
                        ELSE 0
                    END AS value_changed
                FROM ordered_data
            ),
            run_groups AS (
                SELECT 
                    group_col,
                    date_col,
                    value_col,
                    row_num,
                    SUM(value_changed) OVER (PARTITION BY group_col ORDER BY date_col ROWS UNBOUNDED PRECEDING) AS run_id
                FROM value_changes
            ),
            run_lengths AS (
                SELECT 
                    group_col,
                    run_id,
                    value_col,
                    COUNT(*) AS run_length,
                    MIN(row_num) AS run_start_row,
                    MAX(row_num) AS run_end_row
                FROM run_groups
                GROUP BY group_col, run_id, value_col
            ),
            non_zero_run_lengths AS (
                SELECT 
                    group_col,
                    run_id,
                    value_col,
                    COUNT(*) AS run_length,
                    MIN(row_num) AS run_start_row,
                    MAX(row_num) AS run_end_row
                FROM run_groups
                WHERE value_col != 0 OR value_col IS NULL
                GROUP BY group_col, run_id, value_col
            ),
            zeros_start AS (
                SELECT 
                    group_col,
                    COALESCE(MAX(CASE WHEN value_col = 0 AND run_start_row = 1 THEN run_length END), 0) AS n_zeros_start
                FROM run_lengths
                GROUP BY group_col
            ),
            zeros_end AS (
                SELECT 
                    rl.group_col,
                    COALESCE(MAX(CASE WHEN rl.value_col = 0 AND rl.run_end_row = od.max_row THEN rl.run_length END), 0) AS n_zeros_end
                FROM run_lengths rl
                INNER JOIN (
                    SELECT group_col, MAX(row_num) AS max_row
                    FROM ordered_data
                    GROUP BY group_col
                ) od ON rl.group_col = od.group_col
                GROUP BY rl.group_col
            ),
            aggregated_stats AS (
                SELECT 
                    group_col AS series_id,
                    COUNT(*) AS length,
                    MIN(date_col) AS start_date,
                    MAX(date_col) AS end_date,
                    CASE 
                        WHEN MAX(date_col) >= MIN(date_col)
                        THEN CAST(DATEDIFF('day', MIN(date_col), MAX(date_col)) AS INTEGER) + 1
                        ELSE 1
                    END AS expected_length,
                    ROUND(AVG(value_col), 2) AS mean,
                    ROUND(STDDEV(value_col), 2) AS std,
                    ROUND(MIN(value_col), 2) AS min,
                    ROUND(MAX(value_col), 2) AS max,
                    ROUND(MEDIAN(value_col), 2) AS median,
                    COUNT(CASE WHEN value_col IS NULL THEN 1 END) AS n_null,
                    COUNT(CASE WHEN value_col = 0 THEN 1 END) AS n_zeros,
                    COUNT(DISTINCT value_col) AS n_unique_values,
                    COUNT(DISTINCT value_col) = 1 AS is_constant
                FROM QUERY_TABLE(table_name)
                GROUP BY group_col
            ),
            plateau_stats AS (
                SELECT 
                    group_col,
                    MAX(run_length) AS plateau_size
                FROM run_lengths
                GROUP BY group_col
            ),
            plateau_non_zero_stats AS (
                SELECT 
                    group_col,
                    COALESCE(MAX(run_length), 0) AS plateau_size_non_zero
                FROM non_zero_run_lengths
                GROUP BY group_col
            )
            SELECT 
                a.series_id,
                a.length,
                a.start_date,
                a.end_date,
                a.expected_length,
                a.mean,
                a.std,
                a.min,
                a.max,
                a.median,
                a.n_null,
                a.n_zeros,
                a.n_unique_values,
                a.is_constant,
                COALESCE(p.plateau_size, 0) AS plateau_size,
                COALESCE(pnz.plateau_size_non_zero, 0) AS plateau_size_non_zero,
                COALESCE(zs.n_zeros_start, 0) AS n_zeros_start,
                COALESCE(ze.n_zeros_end, 0) AS n_zeros_end
            FROM aggregated_stats a
            LEFT JOIN plateau_stats p ON a.series_id = p.group_col
            LEFT JOIN plateau_non_zero_stats pnz ON a.series_id = pnz.group_col
            LEFT JOIN zeros_start zs ON a.series_id = zs.group_col
            LEFT JOIN zeros_end ze ON a.series_id = ze.group_col
            ORDER BY a.series_id
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
