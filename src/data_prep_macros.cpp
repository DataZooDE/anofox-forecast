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
            SELECT 
                group_col,
                date_col,
                COALESCE(value_col, 
                        LAST_VALUE(value_col IGNORE NULLS) 
                            OVER (PARTITION BY group_col ORDER BY date_col 
                                  ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
                ) AS value_col
            FROM QUERY_TABLE(table_name)
            ORDER BY group_col, date_col
        )"},

    // TS_FILL_NULLS_BACKWARD: Backward fill
    {DEFAULT_SCHEMA,
     "ts_fill_nulls_backward",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            SELECT 
                group_col,
                date_col,
                COALESCE(value_col, 
                        FIRST_VALUE(value_col IGNORE NULLS) 
                            OVER (PARTITION BY group_col ORDER BY date_col 
                                  ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)
                ) AS value_col
            FROM QUERY_TABLE(table_name)
            ORDER BY group_col, date_col
        )"},

    // TS_FILL_NULLS_MEAN: Fill with series mean
    {DEFAULT_SCHEMA,
     "ts_fill_nulls_mean",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH base_with_alias AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid
                FROM QUERY_TABLE(table_name)
            ),
            series_means AS (
                SELECT 
                    __gid,
                    AVG(__vid) AS __mean
                FROM base_with_alias
                WHERE __vid IS NOT NULL
                GROUP BY __gid
            ),
            with_means AS (
                SELECT 
                    b.__gid,
                    b.__did,
                    b.__vid,
                    sm.__mean
                FROM base_with_alias b
                LEFT JOIN series_means sm ON b.__gid = sm.__gid
            )
            SELECT 
                __gid AS group_col,
                __did AS date_col,
                COALESCE(__vid, __mean) AS value_col
            FROM with_means
            ORDER BY __gid, __did
        )"},

    // TS_FILL_GAPS: Fill missing time gaps with NULL
    // Note: This macro generates a full date range for each group
    {DEFAULT_SCHEMA,
     "ts_fill_gaps",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH base_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid,
                    group_col,
                    date_col,
                    value_col
                FROM QUERY_TABLE(table_name)
            ),
            series_ranges AS (
                SELECT DISTINCT
                    __gid,
                    MIN(__did) OVER (PARTITION BY __gid) AS __min,
                    MAX(__did) OVER (PARTITION BY __gid) AS __max
                FROM base_aliased
            ),
            expanded AS (
                SELECT 
                    __gid,
                    UNNEST(GENERATE_SERIES(__min, __max, INTERVAL '1 day')) AS __did
                FROM series_ranges
            )
            SELECT 
                e.__gid AS group_col,
                e.__did AS date_col,
                b.__vid AS value_col
            FROM expanded e
            LEFT JOIN base_aliased b ON e.__gid = b.__gid AND e.__did = b.__did
            ORDER BY e.__gid, e.__did
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

    // TS_FILL_FORWARD: Extend all series to a target date
    {DEFAULT_SCHEMA,
     "ts_fill_forward",
     {"table_name", "group_col", "date_col", "value_col", "target_date", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH base_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid
                FROM QUERY_TABLE(table_name)
            ),
            series_ranges AS (
                SELECT DISTINCT
                    __gid,
                    MIN(__did) OVER (PARTITION BY __gid) AS __min,
                    MAX(__did) OVER (PARTITION BY __gid) AS __max
                FROM base_aliased
            ),
            target_dates AS (
                SELECT 
                    sr.__gid,
                    sr.__min,
                    target_date AS __target
                FROM series_ranges sr
            ),
            expanded AS (
                SELECT 
                    td.__gid,
                    UNNEST(GENERATE_SERIES(td.__min, td.__target, INTERVAL '1 day')) AS __did
                FROM target_dates td
            )
            SELECT 
                e.__gid AS group_col,
                e.__did AS date_col,
                b.__vid AS value_col
            FROM expanded e
            LEFT JOIN base_aliased b ON e.__gid = b.__gid AND e.__did = b.__did
            ORDER BY e.__gid, e.__did
        )"},

    // TS_DROP_GAPPY: Drop series with excessive gaps
    {DEFAULT_SCHEMA,
     "ts_drop_gappy",
     {"table_name", "group_col", "date_col", "max_gap_pct", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH base_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    group_col,
                    date_col
                FROM QUERY_TABLE(table_name)
            ),
            series_ranges AS (
                SELECT 
                    __gid,
                    MIN(__did) AS __min,
                    MAX(__did) AS __max,
                    COUNT(*) AS actual_count
                FROM base_aliased
                GROUP BY __gid
            ),
            expected_counts AS (
                SELECT 
                    __gid,
                    __min,
                    __max,
                    actual_count,
                    CASE 
                        WHEN __max >= __min
                        THEN CAST(DATEDIFF('day', __min, __max) AS INTEGER) + 1
                        ELSE 1
                    END AS expected_count
                FROM series_ranges
            ),
            gap_stats AS (
                SELECT 
                    __gid,
                    actual_count,
                    expected_count,
                    CASE 
                        WHEN expected_count > 0
                        THEN 100.0 * (expected_count - actual_count) / expected_count
                        ELSE 0.0
                    END AS gap_pct
                FROM expected_counts
            ),
            valid_series AS (
                SELECT 
                    __gid
                FROM gap_stats
                WHERE gap_pct <= CAST(max_gap_pct AS DOUBLE)
            ),
            orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    *
                FROM QUERY_TABLE(table_name)
            ),
            filtered AS (
                SELECT *
                FROM orig_aliased oa
                WHERE EXISTS (SELECT 1 FROM valid_series vs WHERE vs.__gid = oa.__gid)
            )
            SELECT 
                f.group_col AS group_col,
                f.* EXCLUDE (__gid)
            FROM filtered f
        )"},

    // TS_DROP_EDGE_ZEROS: Remove both leading and trailing zeros
    {DEFAULT_SCHEMA,
     "ts_drop_edge_zeros",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH with_bounds AS (
                SELECT 
                    group_col,
                    date_col,
                    value_col,
                    MIN(CASE WHEN value_col != 0 THEN date_col END) OVER (PARTITION BY group_col) AS __first_nz,
                    MAX(CASE WHEN value_col != 0 THEN date_col END) OVER (PARTITION BY group_col) AS __last_nz
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                group_col,
                date_col,
                value_col
            FROM with_bounds
            WHERE (__first_nz IS NULL OR date_col >= __first_nz)
              AND (__last_nz IS NULL OR date_col <= __last_nz)
            ORDER BY group_col, date_col
        )"},

    // TS_FILL_NULLS_CONST: Fill with constant value
    {DEFAULT_SCHEMA,
     "ts_fill_nulls_const",
     {"table_name", "group_col", "date_col", "value_col", "fill_value", nullptr},
     {{nullptr, nullptr}},
     R"(
            SELECT 
                group_col,
                date_col,
                COALESCE(value_col, fill_value) AS value_col
            FROM QUERY_TABLE(table_name)
            ORDER BY group_col, date_col
        )"},

    // TS_FILL_NULLS_INTERPOLATE: Linear interpolation
    {DEFAULT_SCHEMA,
     "ts_fill_nulls_interpolate",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH ordered_data AS (
                SELECT 
                    group_col,
                    date_col,
                    value_col,
                    ROW_NUMBER() OVER (PARTITION BY group_col ORDER BY date_col) AS row_num,
                    LAG(value_col) OVER (PARTITION BY group_col ORDER BY date_col) AS prev_val,
                    LEAD(value_col) OVER (PARTITION BY group_col ORDER BY date_col) AS next_val,
                    LAG(date_col) OVER (PARTITION BY group_col ORDER BY date_col) AS prev_date,
                    LEAD(date_col) OVER (PARTITION BY group_col ORDER BY date_col) AS next_date
                FROM QUERY_TABLE(table_name)
            ),
            interpolated AS (
                SELECT 
                    group_col,
                    date_col,
                    CASE 
                        WHEN value_col IS NOT NULL THEN value_col
                        WHEN prev_val IS NOT NULL AND next_val IS NOT NULL AND prev_date IS NOT NULL AND next_date IS NOT NULL
                        THEN prev_val + (next_val - prev_val) * 
                             CAST(DATEDIFF('day', prev_date, date_col) AS DOUBLE) / 
                             CAST(DATEDIFF('day', prev_date, next_date) AS DOUBLE)
                        WHEN prev_val IS NOT NULL THEN prev_val
                        WHEN next_val IS NOT NULL THEN next_val
                        ELSE NULL
                    END AS value_col
                FROM ordered_data
            )
            SELECT 
                group_col,
                date_col,
                value_col
            FROM interpolated
            ORDER BY group_col, date_col
        )"},

    // TS_TRANSFORM_LOG: Log transformation
    {DEFAULT_SCHEMA,
     "ts_transform_log",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            SELECT 
                group_col,
                date_col,
                CASE 
                    WHEN value_col IS NULL THEN NULL
                    WHEN value_col > 0 THEN LN(value_col)
                    ELSE NULL
                END AS value_col
            FROM QUERY_TABLE(table_name)
            ORDER BY group_col, date_col
        )"},

    // TS_DIFF: Differencing - Using 1st order difference only (order parameter causes parser issues)
    {DEFAULT_SCHEMA,
     "ts_diff",
     {"table_name", "group_col", "date_col", "value_col", "order", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH ordered_data AS (
                SELECT 
                    group_col,
                    date_col,
                    value_col,
                    LAG(value_col, 1) OVER (PARTITION BY group_col ORDER BY date_col) AS lagged_value
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                group_col,
                date_col,
                CASE 
                    WHEN value_col IS NULL OR lagged_value IS NULL THEN NULL
                    ELSE value_col - lagged_value
                END AS value_col
            FROM ordered_data
            ORDER BY group_col, date_col
        )"},

    // TS_NORMALIZE_MINMAX: Min-Max normalization (per series)
    {DEFAULT_SCHEMA,
     "ts_normalize_minmax",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_stats AS (
                SELECT 
                    group_col AS __gid,
                    MIN(value_col) AS __min_val,
                    MAX(value_col) AS __max_val,
                    MAX(value_col) - MIN(value_col) AS __range
                FROM QUERY_TABLE(table_name)
                WHERE value_col IS NOT NULL
                GROUP BY group_col
            ),
            with_stats AS (
                SELECT 
                    t.group_col,
                    t.date_col,
                    t.value_col,
                    ss.__min_val,
                    ss.__range
                FROM QUERY_TABLE(table_name) t
                LEFT JOIN series_stats ss ON t.group_col = ss.__gid
            )
            SELECT 
                group_col,
                date_col,
                CASE 
                    WHEN value_col IS NULL THEN NULL
                    WHEN __range = 0 THEN 0.0
                    ELSE (value_col - __min_val) / __range
                END AS value_col
            FROM with_stats
            ORDER BY group_col, date_col
        )"},

    // TS_STANDARDIZE: Z-score standardization (per series)
    {DEFAULT_SCHEMA,
     "ts_standardize",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH series_stats AS (
                SELECT 
                    group_col AS __gid,
                    AVG(value_col) AS __mean,
                    STDDEV(value_col) AS __std
                FROM QUERY_TABLE(table_name)
                WHERE value_col IS NOT NULL
                GROUP BY group_col
            ),
            with_stats AS (
                SELECT 
                    t.group_col,
                    t.date_col,
                    t.value_col,
                    ss.__mean,
                    ss.__std
                FROM QUERY_TABLE(table_name) t
                LEFT JOIN series_stats ss ON t.group_col = ss.__gid
            )
            SELECT 
                group_col,
                date_col,
                CASE 
                    WHEN value_col IS NULL THEN NULL
                    WHEN __std IS NULL OR __std = 0 THEN 0.0
                    ELSE (value_col - __mean) / __std
                END AS value_col
            FROM with_stats
            ORDER BY group_col, date_col
        )"},

    // End marker
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

// Register Data Preparation table macros
void RegisterDataPrepMacros(ExtensionLoader &loader) {
	for (idx_t index = 0; data_prep_macros[index].name != nullptr; index++) {
		auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(data_prep_macros[index]);
		table_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
		loader.RegisterFunction(*table_info);
	}
}

} // namespace duckdb
