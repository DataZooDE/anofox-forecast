#include "duckdb.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"

namespace duckdb {

// Array of data preparation macros - all follow consistent signature:
// (table_name, group_col, date_col, value_col)
static const DefaultTableMacro data_prep_macros[] = {

    // TS_FILL_NULLS_FORWARD: Forward fill (LOCF)
    {DEFAULT_SCHEMA,
     "ts_fill_nulls_forward",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH aliased AS (
                SELECT 
                    group_col AS __g,
                    date_col AS __d,
                    value_col AS __v
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                __g AS group_col,
                __d AS date_col,
                COALESCE(__v, 
                        LAST_VALUE(__v IGNORE NULLS) 
                            OVER (PARTITION BY __g ORDER BY __d 
                                  ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
                ) AS value_col
            FROM aliased
            ORDER BY __g, __d
        )"},

    // TS_FILL_NULLS_BACKWARD: Backward fill
    {DEFAULT_SCHEMA,
     "ts_fill_nulls_backward",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH aliased AS (
                SELECT 
                    group_col AS __g,
                    date_col AS __d,
                    value_col AS __v
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                __g AS group_col,
                __d AS date_col,
                COALESCE(__v, 
                        FIRST_VALUE(__v IGNORE NULLS) 
                            OVER (PARTITION BY __g ORDER BY __d 
                                  ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)
                ) AS value_col
            FROM aliased
            ORDER BY __g, __d
        )"},

    // TS_FILL_NULLS_MEAN: Fill with series mean
    {DEFAULT_SCHEMA,
     "ts_fill_nulls_mean",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_means AS (
                SELECT 
                    group_col AS __g,
                    AVG(value_col) AS __mean
                FROM QUERY_TABLE(table_name)
                WHERE value_col IS NOT NULL
                GROUP BY group_col
            ),
            with_means AS (
                SELECT 
                    group_col AS __g2,
                    date_col AS __d2,
                    value_col AS __v2,
                    __mean
                FROM QUERY_TABLE(table_name)
                LEFT JOIN series_means ON group_col = __g
            )
            SELECT 
                __g2 AS group_col,
                __d2 AS date_col,
                COALESCE(__v2, __mean) AS value_col
            FROM with_means
            ORDER BY __g2, __d2
        )"},

    // TS_FILL_GAPS: Fill missing time gaps with NULL
    // Note: This macro generates a full date range for each group
    {DEFAULT_SCHEMA,
     "ts_fill_gaps",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_ranges AS (
                SELECT DISTINCT
                    group_col,
                    MIN(date_col) OVER (PARTITION BY group_col) AS __min,
                    MAX(date_col) OVER (PARTITION BY group_col) AS __max
                FROM QUERY_TABLE(table_name)
            ),
            expanded AS (
                SELECT 
                    group_col AS __g,
                    UNNEST(GENERATE_SERIES(__min, __max, INTERVAL '1 day')) AS __d
                FROM series_ranges
            ),
            original AS (
                SELECT 
                    group_col AS __g2,
                    date_col AS __d2,
                    value_col AS __v2
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                __g AS group_col,
                __d AS date_col,
                __v2 AS value_col
            FROM expanded
            LEFT JOIN original ON __g = __g2 AND __d = __d2
            ORDER BY __g, __d
        )"},

    // TS_DROP_CONSTANT: Drop constant series
    {DEFAULT_SCHEMA,
     "ts_drop_constant",
     {"table_name", "group_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_variance AS (
                SELECT 
                    group_col AS __gid
                FROM QUERY_TABLE(table_name)
                GROUP BY group_col
                HAVING COUNT(DISTINCT value_col) > 1
            ),
            orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    *
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                oa.* EXCLUDE (__gid)
            FROM orig_aliased oa
            WHERE EXISTS (SELECT 1 FROM series_variance sv WHERE sv.__gid = oa.__gid)
        )"},

    // TS_DROP_SHORT: Drop short series
    {DEFAULT_SCHEMA,
     "ts_drop_short",
     {"table_name", "group_col", "min_length", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_length AS (
                SELECT 
                    group_col AS __gid
                FROM QUERY_TABLE(table_name)
                GROUP BY group_col
                HAVING COUNT(*) >= min_length
            ),
            orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    *
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                oa.* EXCLUDE (__gid)
            FROM orig_aliased oa
            WHERE EXISTS (SELECT 1 FROM series_length sl WHERE sl.__gid = oa.__gid)
        )"},

    // TS_DROP_ZEROS: Drop series with all zeros
    {DEFAULT_SCHEMA,
     "ts_drop_zeros",
     {"table_name", "group_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH non_zero_series AS (
                SELECT 
                    group_col AS __gid
                FROM QUERY_TABLE(table_name)
                GROUP BY group_col
                HAVING SUM(CASE WHEN value_col != 0 THEN 1 ELSE 0 END) > 0
            ),
            orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    *
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                oa.* EXCLUDE (__gid)
            FROM orig_aliased oa
            WHERE EXISTS (SELECT 1 FROM non_zero_series nz WHERE nz.__gid = oa.__gid)
        )"},

    // TS_DROP_LEADING_ZEROS: Remove leading zeros
    {DEFAULT_SCHEMA,
     "ts_drop_leading_zeros",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH with_first_nonzero AS (
                SELECT 
                    group_col,
                    date_col,
                    value_col,
                    MIN(CASE WHEN value_col != 0 THEN date_col END) OVER (PARTITION BY group_col) AS __first_nz
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                group_col,
                date_col,
                value_col
            FROM with_first_nonzero
            WHERE date_col >= __first_nz OR __first_nz IS NULL
            ORDER BY group_col, date_col
        )"},

    // TS_DROP_TRAILING_ZEROS: Remove trailing zeros
    {DEFAULT_SCHEMA,
     "ts_drop_trailing_zeros",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH with_last_nonzero AS (
                SELECT 
                    group_col,
                    date_col,
                    value_col,
                    MAX(CASE WHEN value_col != 0 THEN date_col END) OVER (PARTITION BY group_col) AS __last_nz
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                group_col,
                date_col,
                value_col
            FROM with_last_nonzero
            WHERE date_col <= __last_nz OR __last_nz IS NULL
            ORDER BY group_col, date_col
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
