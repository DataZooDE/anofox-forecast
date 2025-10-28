#include "duckdb.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"

namespace duckdb {

// Array of data preparation macros
static const DefaultTableMacro data_prep_macros[] = {

    // TS_FILL_GAPS: Fill missing time gaps
    {DEFAULT_SCHEMA,
     "ts_fill_gaps",
     {"table_name", "group_cols", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_data AS (
                SELECT * FROM QUERY_TABLE(table_name)
            ),
            date_ranges AS (
                SELECT 
                    group_cols AS series_id,
                    MIN(date_col) AS min_date,
                    MAX(date_col) AS max_date
                FROM series_data
                GROUP BY group_cols
            ),
            full_dates AS (
                SELECT 
                    series_id,
                    UNNEST(GENERATE_SERIES(min_date, max_date, INTERVAL '1 day')) AS date_col
                FROM date_ranges
            ),
            filled AS (
                SELECT 
                    f.series_id AS group_cols,
                    f.date_col,
                    s.value_col
                FROM full_dates f
                LEFT JOIN series_data s 
                    ON f.series_id = s.group_cols 
                    AND f.date_col = s.date_col
            )
            SELECT 
                group_cols,
                CAST(date_col AS DATE) AS date_col,
                value_col
            FROM filled
            ORDER BY group_cols, date_col
        )"},

    // TS_FILL_FORWARD: Extend series to target date
    {DEFAULT_SCHEMA,
     "ts_fill_forward",
     {"table_name", "group_cols", "date_col", "value_col", "target_date", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_data AS (
                SELECT * FROM QUERY_TABLE(table_name)
            ),
            max_dates AS (
                SELECT 
                    group_cols AS series_id,
                    MAX(date_col) AS max_date
                FROM series_data
                GROUP BY group_cols
            ),
            extended_dates AS (
                SELECT 
                    series_id,
                    UNNEST(GENERATE_SERIES(max_date + INTERVAL '1 day', target_date, INTERVAL '1 day')) AS date_col
                FROM max_dates
                WHERE max_date < target_date
            ),
            new_rows AS (
                SELECT 
                    series_id AS group_cols,
                    CAST(date_col AS DATE) AS date_col,
                    NULL AS value_col
                FROM extended_dates
            )
            SELECT * FROM series_data
            UNION ALL
            SELECT * FROM new_rows
            ORDER BY group_cols, date_col
        )"},

    // TS_DROP_CONSTANT: Drop constant series
    {DEFAULT_SCHEMA, "ts_drop_constant", {"table_name", "group_cols", "value_col", nullptr}, {{nullptr, nullptr}}, R"(
            WITH series_data AS (
                SELECT * FROM QUERY_TABLE(table_name)
            ),
            series_variance AS (
                SELECT 
                    group_cols AS series_id,
                    COUNT(DISTINCT value_col) AS n_unique
                FROM series_data
                GROUP BY group_cols
                HAVING COUNT(DISTINCT value_col) > 1
            )
            SELECT s.*
            FROM series_data s
            INNER JOIN series_variance v 
                ON s.group_cols = v.series_id
        )"},

    // TS_DROP_SHORT: Drop short series
    {DEFAULT_SCHEMA,
     "ts_drop_short",
     {"table_name", "group_cols", "date_col", "min_length", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_data AS (
                SELECT * FROM QUERY_TABLE(table_name)
            ),
            series_length AS (
                SELECT 
                    group_cols AS series_id,
                    COUNT(*) AS length
                FROM series_data
                GROUP BY group_cols
                HAVING COUNT(*) >= min_length
            )
            SELECT s.*
            FROM series_data s
            INNER JOIN series_length l 
                ON s.group_cols = l.series_id
        )"},

    // TS_DROP_GAPPY: Drop series with excessive gaps
    {DEFAULT_SCHEMA,
     "ts_drop_gappy",
     {"table_name", "group_cols", "date_col", "max_gap_pct", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_data AS (
                SELECT * FROM QUERY_TABLE(table_name)
            ),
            series_gaps AS (
                SELECT 
                    group_cols AS series_id,
                    COUNT(*) AS actual_length,
                    DATE_DIFF('day', MIN(date_col), MAX(date_col)) + 1 AS expected_length,
                    1.0 - (CAST(COUNT(*) AS DOUBLE) / (DATE_DIFF('day', MIN(date_col), MAX(date_col)) + 1)) AS gap_ratio
                FROM series_data
                GROUP BY group_cols
                HAVING gap_ratio <= max_gap_pct
            )
            SELECT s.*
            FROM series_data s
            INNER JOIN series_gaps g 
                ON s.group_cols = g.series_id
        )"},

    // TS_DROP_LEADING_ZEROS: Remove leading zeros
    {DEFAULT_SCHEMA,
     "ts_drop_leading_zeros",
     {"table_name", "group_cols", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_data AS (
                SELECT * FROM QUERY_TABLE(table_name)
            ),
            with_first_nonzero AS (
                SELECT 
                    group_cols,
                    date_col,
                    value_col,
                    MIN(CASE WHEN value_col != 0 THEN date_col END) 
                        OVER (PARTITION BY group_cols) AS first_nonzero_date
                FROM series_data
            )
            SELECT 
                group_cols,
                date_col,
                value_col
            FROM with_first_nonzero
            WHERE date_col >= first_nonzero_date OR first_nonzero_date IS NULL
            ORDER BY group_cols, date_col
        )"},

    // TS_DROP_TRAILING_ZEROS: Remove trailing zeros
    {DEFAULT_SCHEMA,
     "ts_drop_trailing_zeros",
     {"table_name", "group_cols", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_data AS (
                SELECT * FROM QUERY_TABLE(table_name)
            ),
            with_last_nonzero AS (
                SELECT 
                    group_cols,
                    date_col,
                    value_col,
                    MAX(CASE WHEN value_col != 0 THEN date_col END) 
                        OVER (PARTITION BY group_cols) AS last_nonzero_date
                FROM series_data
            )
            SELECT 
                group_cols,
                date_col,
                value_col
            FROM with_last_nonzero
            WHERE date_col <= last_nonzero_date OR last_nonzero_date IS NULL
            ORDER BY group_cols, date_col
        )"},

    // TS_DROP_EDGE_ZEROS: Drop both leading and trailing zeros
    {DEFAULT_SCHEMA,
     "ts_drop_edge_zeros",
     {"table_name", "group_cols", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_data AS (
                SELECT * FROM QUERY_TABLE(table_name)
            ),
            with_nonzero_range AS (
                SELECT 
                    group_cols,
                    date_col,
                    value_col,
                    MIN(CASE WHEN value_col != 0 THEN date_col END) 
                        OVER (PARTITION BY group_cols) AS first_nonzero_date,
                    MAX(CASE WHEN value_col != 0 THEN date_col END) 
                        OVER (PARTITION BY group_cols) AS last_nonzero_date
                FROM series_data
            )
            SELECT 
                group_cols,
                date_col,
                value_col
            FROM with_nonzero_range
            WHERE (date_col >= first_nonzero_date AND date_col <= last_nonzero_date)
               OR (first_nonzero_date IS NULL AND last_nonzero_date IS NULL)
            ORDER BY group_cols, date_col
        )"},

    // TS_FILL_NULLS_CONST: Fill with constant value
    {DEFAULT_SCHEMA,
     "ts_fill_nulls_const",
     {"table_name", "group_cols", "date_col", "value_col", "fill_value", nullptr},
     {{nullptr, nullptr}},
     R"(
            SELECT 
                group_cols,
                date_col,
                COALESCE(value_col, fill_value) AS value_col
            FROM QUERY_TABLE(table_name)
        )"},

    // TS_FILL_NULLS_FORWARD: Forward fill (LOCF)
    {DEFAULT_SCHEMA,
     "ts_fill_nulls_forward",
     {"table_name", "group_cols", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_data AS (
                SELECT * FROM QUERY_TABLE(table_name)
                ORDER BY group_cols, date_col
            )
            SELECT 
                group_cols,
                date_col,
                COALESCE(value_col, 
                        LAST_VALUE(value_col IGNORE NULLS) 
                            OVER (PARTITION BY group_cols ORDER BY date_col 
                                  ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
                ) AS value_col
            FROM series_data
        )"},

    // TS_FILL_NULLS_BACKWARD: Backward fill
    {DEFAULT_SCHEMA,
     "ts_fill_nulls_backward",
     {"table_name", "group_cols", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_data AS (
                SELECT * FROM QUERY_TABLE(table_name)
                ORDER BY group_cols, date_col
            )
            SELECT 
                group_cols,
                date_col,
                COALESCE(value_col, 
                        FIRST_VALUE(value_col IGNORE NULLS) 
                            OVER (PARTITION BY group_cols ORDER BY date_col 
                                  ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)
                ) AS value_col
            FROM series_data
        )"},

    // TS_FILL_NULLS_MEAN: Fill with series mean
    {DEFAULT_SCHEMA,
     "ts_fill_nulls_mean",
     {"table_name", "group_cols", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_data AS (
                SELECT * FROM QUERY_TABLE(table_name)
            ),
            series_means AS (
                SELECT 
                    group_cols AS series_id,
                    AVG(value_col) AS mean_val
                FROM series_data
                WHERE value_col IS NOT NULL
                GROUP BY group_cols
            )
            SELECT 
                s.group_cols,
                s.date_col,
                COALESCE(s.value_col, m.mean_val) AS value_col
            FROM series_data s
            LEFT JOIN series_means m ON s.group_cols = m.series_id
        )"},

    // End marker
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

// Register Data Preparation table macros
void RegisterDataPrepMacros(ExtensionLoader &loader) {
	for (idx_t index = 0; data_prep_macros[index].name != nullptr; index++) {
		auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(data_prep_macros[index]);
		loader.RegisterFunction(*table_info);
	}
}

} // namespace duckdb
