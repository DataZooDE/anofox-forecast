#include "duckdb.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/function/table_function.hpp"
#include "data_prep_bind_replace.hpp"
#include "ts_fill_gaps_function.hpp"
#include <map>
#include <set>
#include <vector>

namespace duckdb {

// Array of data preparation macros - all follow consistent signature:
// (table_name, group_col, date_col, value_col)
static const DefaultTableMacro data_prep_macros[] = {

    // TS_FILL_NULLS_FORWARD: Forward fill (LOCF)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_nulls_forward",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid,
                    *
                FROM QUERY_TABLE(table_name)
            ),
            with_filled AS (
                SELECT 
                    __gid,
                    __did,
                    __vid,
                    COALESCE(__vid, 
                            LAST_VALUE(__vid IGNORE NULLS) 
                                OVER (PARTITION BY __gid ORDER BY __did 
                                      ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW)
                    ) AS __filled_vid,
                    orig_aliased.* EXCLUDE (__gid, __did, __vid)
                FROM orig_aliased
            )
            SELECT 
                with_filled.* EXCLUDE (__gid, __did, __vid, __filled_vid),
                with_filled.__gid AS group_col,
                with_filled.__did AS date_col,
                with_filled.__filled_vid AS value_col
            FROM with_filled
            ORDER BY group_col, date_col
        )"},

    // TS_FILL_NULLS_BACKWARD: Backward fill
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_nulls_backward",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid,
                    *
                FROM QUERY_TABLE(table_name)
            ),
            with_filled AS (
                SELECT 
                    __gid,
                    __did,
                    __vid,
                    COALESCE(__vid, 
                            FIRST_VALUE(__vid IGNORE NULLS) 
                                OVER (PARTITION BY __gid ORDER BY __did 
                                      ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING)
                    ) AS __filled_vid,
                    orig_aliased.* EXCLUDE (__gid, __did, __vid)
                FROM orig_aliased
            )
            SELECT 
                with_filled.* EXCLUDE (__gid, __did, __vid, __filled_vid),
                with_filled.__gid AS group_col,
                with_filled.__did AS date_col,
                with_filled.__filled_vid AS value_col
            FROM with_filled
            ORDER BY group_col, date_col
        )"},

    // TS_FILL_NULLS_MEAN: Fill with series mean
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_nulls_mean",
     {"table_name", "group_col", "date_col", "value_col", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid,
                    *
                FROM QUERY_TABLE(table_name)
            ),
            series_means AS (
                SELECT 
                    __gid,
                    AVG(__vid) AS __mean
                FROM orig_aliased
                WHERE __vid IS NOT NULL
                GROUP BY __gid
            ),
            with_means AS (
                SELECT 
                    oa.__gid,
                    oa.__did,
                    oa.__vid,
                    sm.__mean,
                    oa.* EXCLUDE (__gid, __did, __vid)
                FROM orig_aliased oa
                LEFT JOIN series_means sm ON oa.__gid = sm.__gid
            )
            SELECT 
                with_means.* EXCLUDE (__gid, __did, __vid, __mean),
                with_means.__gid AS group_col,
                with_means.__did AS date_col,
                COALESCE(with_means.__vid, with_means.__mean) AS value_col
            FROM with_means
            ORDER BY group_col, date_col
        )"},

    // TS_FILL_GAPS: Fill missing time gaps with NULL (VARCHAR frequency - date-based)
    // Note: This macro generates a full date range for each group
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_gaps",
     {"table_name", "group_col", "date_col", "value_col", "frequency", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid,
                    *
                FROM QUERY_TABLE(table_name)
            ),
            frequency_parsed AS (
                SELECT 
                    frequency,
                    CASE 
                        WHEN frequency IS NULL THEN INTERVAL '1 day'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1D', '1DAY') THEN INTERVAL '1 day'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('30M', '30MIN', '30MINUTE', '30MINUTES') THEN INTERVAL '30 minutes'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1H', '1HOUR', '1HOURS') THEN INTERVAL '1 hour'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1W', '1WEEK', '1WEEKS') THEN INTERVAL '1 week'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1MO', '1MONTH', '1MONTHS') THEN INTERVAL '1 month'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1Q', '1QUARTER', '1QUARTERS') THEN INTERVAL '3 months'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1Y', '1YEAR', '1YEARS') THEN INTERVAL '1 year'
                        ELSE INTERVAL '1 day'
                    END AS __interval
                FROM (SELECT 1) t
            ),
            date_type_check AS (
                SELECT DISTINCT
                    __gid,
                    -- Check if original date_col is DATE type by comparing value to its DATE cast
                    -- For DATE: value = CAST(value AS DATE) is always true
                    -- For TIMESTAMP: this is only true if time component is 00:00:00
                    -- We use MIN to get a representative value
                    MIN(__did) = CAST(MIN(__did) AS DATE) AS __is_date_type
                FROM orig_aliased
                GROUP BY __gid
            ),
            series_ranges AS (
                SELECT DISTINCT
                    oa.__gid,
                    MIN(oa.__did) OVER (PARTITION BY oa.__gid) AS __min,
                    MAX(oa.__did) OVER (PARTITION BY oa.__gid) AS __max,
                    dtc.__is_date_type
                FROM orig_aliased oa
                LEFT JOIN date_type_check dtc ON oa.__gid = dtc.__gid
            ),
            expanded AS (
                SELECT 
                    sr.__gid,
                    UNNEST(GENERATE_SERIES(sr.__min, sr.__max, fp.__interval)) AS __did,
                    sr.__is_date_type
                FROM series_ranges sr
                CROSS JOIN frequency_parsed fp
            ),
            with_original_data AS (
                SELECT 
                    e.__gid,
                    -- Use original __did if it exists (preserves type), otherwise cast generated date
                    CASE 
                        WHEN oa.__did IS NOT NULL THEN oa.__did
                        WHEN e.__is_date_type THEN CAST(e.__did AS DATE)
                        ELSE e.__did
                    END AS __did,
                    oa.__vid,
                    oa.* EXCLUDE (__gid, __did, __vid)
                FROM expanded e
                LEFT JOIN orig_aliased oa ON e.__gid = oa.__gid 
                    AND (
                        -- For DATE type, cast generated TIMESTAMP to DATE for comparison
                        (e.__is_date_type AND CAST(e.__did AS DATE) = oa.__did)
                        OR
                        -- For TIMESTAMP, direct comparison
                        (NOT e.__is_date_type AND e.__did = oa.__did)
                    )
            )
            SELECT 
                with_original_data.__gid AS group_col,
                with_original_data.__did AS date_col,
                with_original_data.__vid AS value_col
            FROM with_original_data
            ORDER BY group_col, date_col
        )"},

    // TS_FILL_GAPS: Fill missing time gaps with NULL (INTEGER frequency - integer-based)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_gaps",
     {"table_name", "group_col", "date_col", "value_col", "frequency", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid,
                    *
                FROM QUERY_TABLE(table_name)
            ),
            frequency_parsed AS (
                SELECT 
                    COALESCE(frequency, 1) AS __int_step
                FROM (SELECT 1) t
            ),
            series_ranges AS (
                SELECT DISTINCT
                    __gid,
                    MIN(__did) OVER (PARTITION BY __gid) AS __min,
                    MAX(__did) OVER (PARTITION BY __gid) AS __max
                FROM orig_aliased
            ),
            expanded AS (
                SELECT 
                    sr.__gid,
                    UNNEST(GENERATE_SERIES(sr.__min, sr.__max, fp.__int_step)) AS __did
                FROM series_ranges sr
                CROSS JOIN frequency_parsed fp
            ),
            with_original_data AS (
                SELECT 
                    e.__gid,
                    e.__did,
                    oa.__vid,
                    oa.*
                FROM expanded e
                LEFT JOIN orig_aliased oa ON e.__gid = oa.__gid AND e.__did = oa.__did
            )
            SELECT 
                with_original_data.* EXCLUDE (__gid, __did, __vid),
                with_original_data.__gid AS group_col,
                with_original_data.__did AS date_col,
                with_original_data.__vid AS value_col
            FROM with_original_data
            ORDER BY group_col, date_col
        )"},

    // TS_DROP_CONSTANT: Drop constant series
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_drop_constant",
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
     "anofox_fcst_ts_drop_short",
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
     "anofox_fcst_ts_drop_zeros",
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
     "anofox_fcst_ts_drop_leading_zeros",
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
     "anofox_fcst_ts_drop_trailing_zeros",
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

    // TS_FILL_FORWARD: Extend all series to a target date (VARCHAR frequency - date-based)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_forward",
     {"table_name", "group_col", "date_col", "value_col", "target_date", "frequency", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid,
                    *
                FROM QUERY_TABLE(table_name)
            ),
            frequency_parsed AS (
                SELECT 
                    frequency,
                    CASE 
                        WHEN frequency IS NULL THEN INTERVAL '1 day'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1D', '1DAY') THEN INTERVAL '1 day'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('30M', '30MIN', '30MINUTE', '30MINUTES') THEN INTERVAL '30 minutes'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1H', '1HOUR', '1HOURS') THEN INTERVAL '1 hour'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1W', '1WEEK', '1WEEKS') THEN INTERVAL '1 week'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1MO', '1MONTH', '1MONTHS') THEN INTERVAL '1 month'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1Q', '1QUARTER', '1QUARTERS') THEN INTERVAL '3 months'
                        WHEN UPPER(TRIM(CAST(frequency AS VARCHAR))) IN ('1Y', '1YEAR', '1YEARS') THEN INTERVAL '1 year'
                        ELSE INTERVAL '1 day'
                    END AS __interval
                FROM (SELECT 1) t
            ),
            date_type_check AS (
                SELECT DISTINCT
                    __gid,
                    -- Check if original date_col is DATE type by comparing value to its DATE cast
                    -- For DATE: value = CAST(value AS DATE) is always true
                    -- For TIMESTAMP: this is only true if time component is 00:00:00
                    -- We use MIN to get a representative value
                    MIN(__did) = CAST(MIN(__did) AS DATE) AS __is_date_type
                FROM orig_aliased
                GROUP BY __gid
            ),
            series_ranges AS (
                SELECT DISTINCT
                    oa.__gid,
                    MIN(oa.__did) OVER (PARTITION BY oa.__gid) AS __min,
                    MAX(oa.__did) OVER (PARTITION BY oa.__gid) AS __max,
                    dtc.__is_date_type
                FROM orig_aliased oa
                LEFT JOIN date_type_check dtc ON oa.__gid = dtc.__gid
            ),
            target_dates AS (
                SELECT 
                    sr.__gid,
                    sr.__min,
                    sr.__is_date_type,
                    target_date AS __target
                FROM series_ranges sr
            ),
            expanded AS (
                SELECT 
                    td.__gid,
                    UNNEST(GENERATE_SERIES(td.__min, td.__target, fp.__interval)) AS __did,
                    td.__is_date_type
                FROM target_dates td
                CROSS JOIN frequency_parsed fp
            ),
            with_original_data AS (
                SELECT 
                    e.__gid,
                    -- Use original __did if it exists (preserves type), otherwise cast generated date
                    CASE 
                        WHEN oa.__did IS NOT NULL THEN oa.__did
                        WHEN e.__is_date_type THEN CAST(e.__did AS DATE)
                        ELSE e.__did
                    END AS __did,
                    oa.__vid,
                    oa.* EXCLUDE (__gid, __did, __vid)
                FROM expanded e
                LEFT JOIN orig_aliased oa ON e.__gid = oa.__gid 
                    AND (
                        -- For DATE type, cast generated TIMESTAMP to DATE for comparison
                        (e.__is_date_type AND CAST(e.__did AS DATE) = oa.__did)
                        OR
                        -- For TIMESTAMP, direct comparison
                        (NOT e.__is_date_type AND e.__did = oa.__did)
                    )
            )
            SELECT 
                with_original_data.* EXCLUDE (__gid, __did, __vid),
                with_original_data.__gid AS group_col,
                with_original_data.__did AS date_col,
                with_original_data.__vid AS value_col
            FROM with_original_data
            ORDER BY group_col, date_col
        )"},

    // TS_FILL_FORWARD: Extend all series to a target date (INTEGER frequency - integer-based)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_fill_forward",
     {"table_name", "group_col", "date_col", "value_col", "target_date", "frequency", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid,
                    *
                FROM QUERY_TABLE(table_name)
            ),
            frequency_parsed AS (
                SELECT 
                    COALESCE(frequency, 1) AS __int_step
                FROM (SELECT 1) t
            ),
            series_ranges AS (
                SELECT DISTINCT
                    __gid,
                    MIN(__did) OVER (PARTITION BY __gid) AS __min,
                    MAX(__did) OVER (PARTITION BY __gid) AS __max
                FROM orig_aliased
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
                    UNNEST(GENERATE_SERIES(td.__min, td.__target, fp.__int_step)) AS __did
                FROM target_dates td
                CROSS JOIN frequency_parsed fp
            ),
            with_original_data AS (
                SELECT 
                    e.__gid,
                    e.__did,
                    oa.__vid,
                    oa.*
                FROM expanded e
                LEFT JOIN orig_aliased oa ON e.__gid = oa.__gid AND e.__did = oa.__did
            )
            SELECT 
                with_original_data.* EXCLUDE (__gid, __did, __vid),
                with_original_data.__gid AS group_col,
                with_original_data.__did AS date_col,
                with_original_data.__vid AS value_col
            FROM with_original_data
            ORDER BY group_col, date_col
        )"},

    // TS_DROP_GAPPY: Drop series with excessive gaps
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_drop_gappy",
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
            )
            SELECT 
                orig_aliased.__gid AS group_col,
                orig_aliased.* EXCLUDE (__gid)
            FROM orig_aliased
            WHERE EXISTS (SELECT 1 FROM valid_series vs WHERE vs.__gid = orig_aliased.__gid)
        )"},

    // TS_DROP_EDGE_ZEROS: Remove both leading and trailing zeros
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_drop_edge_zeros",
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
     "anofox_fcst_ts_fill_nulls_const",
     {"table_name", "group_col", "date_col", "value_col", "fill_value", nullptr},
     {{nullptr, nullptr}},
     R"(
            WITH orig_aliased AS (
                SELECT 
                    group_col AS __gid,
                    date_col AS __did,
                    value_col AS __vid,
                    fill_value AS __fill_val,
                    *
                FROM QUERY_TABLE(table_name)
            )
            SELECT 
                orig_aliased.* EXCLUDE (__gid, __did, __vid, __fill_val),
                orig_aliased.__gid AS group_col,
                orig_aliased.__did AS date_col,
                COALESCE(orig_aliased.__vid, orig_aliased.__fill_val) AS value_col
            FROM orig_aliased
            ORDER BY group_col, date_col
        )"},

    // TS_DIFF: Differencing - Using 1st order difference only (order parameter causes parser issues)
    {DEFAULT_SCHEMA,
     "anofox_fcst_ts_diff",
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

    // End marker
    {nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}};

// Register Data Preparation table macros
void RegisterDataPrepMacros(ExtensionLoader &loader) {
	// Register all macros with bind_replace for better column preservation
	// This provides dynamic SQL generation that preserves original column names

	// TS_FILL_NULLS_FORWARD: 4 params
	TableFunction fill_nulls_forward(
	    "anofox_fcst_ts_fill_nulls_forward",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, nullptr, nullptr);
	fill_nulls_forward.bind_replace = TSFillNullsForwardBindReplace;
	fill_nulls_forward.named_parameters["table_name"] = LogicalType::VARCHAR;
	fill_nulls_forward.named_parameters["group_col"] = LogicalType::VARCHAR;
	fill_nulls_forward.named_parameters["date_col"] = LogicalType::VARCHAR;
	fill_nulls_forward.named_parameters["value_col"] = LogicalType::VARCHAR;
	loader.RegisterFunction(fill_nulls_forward);

	// TS_FILL_NULLS_BACKWARD: 4 params
	TableFunction fill_nulls_backward(
	    "anofox_fcst_ts_fill_nulls_backward",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, nullptr, nullptr);
	fill_nulls_backward.bind_replace = TSFillNullsBackwardBindReplace;
	fill_nulls_backward.named_parameters["table_name"] = LogicalType::VARCHAR;
	fill_nulls_backward.named_parameters["group_col"] = LogicalType::VARCHAR;
	fill_nulls_backward.named_parameters["date_col"] = LogicalType::VARCHAR;
	fill_nulls_backward.named_parameters["value_col"] = LogicalType::VARCHAR;
	loader.RegisterFunction(fill_nulls_backward);

	// TS_FILL_NULLS_MEAN: 4 params
	TableFunction fill_nulls_mean(
	    "anofox_fcst_ts_fill_nulls_mean",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, nullptr, nullptr);
	fill_nulls_mean.bind_replace = TSFillNullsMeanBindReplace;
	fill_nulls_mean.named_parameters["table_name"] = LogicalType::VARCHAR;
	fill_nulls_mean.named_parameters["group_col"] = LogicalType::VARCHAR;
	fill_nulls_mean.named_parameters["date_col"] = LogicalType::VARCHAR;
	fill_nulls_mean.named_parameters["value_col"] = LogicalType::VARCHAR;
	loader.RegisterFunction(fill_nulls_mean);

	// TS_FILL_NULLS_CONST: 5 params - fill_value accepts ANY type
	TableFunction fill_nulls_const(
	    "anofox_fcst_ts_fill_nulls_const",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::ANY},
	    nullptr, nullptr);
	fill_nulls_const.bind_replace = TSFillNullsConstBindReplace;
	fill_nulls_const.named_parameters["table_name"] = LogicalType::VARCHAR;
	fill_nulls_const.named_parameters["group_col"] = LogicalType::VARCHAR;
	fill_nulls_const.named_parameters["date_col"] = LogicalType::VARCHAR;
	fill_nulls_const.named_parameters["value_col"] = LogicalType::VARCHAR;
	fill_nulls_const.named_parameters["fill_value"] = LogicalType::ANY;
	loader.RegisterFunction(fill_nulls_const);

	// TS_FILL_GAPS: Table-In-Out operator (internal function)
	// This is the native C++ implementation that takes TABLE input
	auto ts_fill_gaps_operator = CreateTSFillGapsOperatorTableFunction();
	TableFunctionSet ts_fill_gaps_operator_set("anofox_fcst_ts_fill_gaps_operator");
	ts_fill_gaps_operator_set.AddFunction(*ts_fill_gaps_operator);
	CreateTableFunctionInfo ts_fill_gaps_operator_info(std::move(ts_fill_gaps_operator_set));
	loader.RegisterFunction(std::move(ts_fill_gaps_operator_info));

	// Register alias for ts_fill_gaps_operator
	auto ts_fill_gaps_operator_alias = CreateTSFillGapsOperatorTableFunction();
	TableFunctionSet ts_fill_gaps_operator_alias_set("ts_fill_gaps_operator");
	ts_fill_gaps_operator_alias_set.AddFunction(*ts_fill_gaps_operator_alias);
	CreateTableFunctionInfo ts_fill_gaps_operator_alias_info(std::move(ts_fill_gaps_operator_alias_set));
	ts_fill_gaps_operator_alias_info.alias_of = "anofox_fcst_ts_fill_gaps_operator";
	ts_fill_gaps_operator_alias_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	loader.RegisterFunction(std::move(ts_fill_gaps_operator_alias_info));

	// TS_FILL_GAPS: Public API with string table name (uses bind_replace to pipe into operator)
	// VARCHAR frequency overload
	TableFunction ts_fill_gaps_varchar(
	    "anofox_fcst_ts_fill_gaps",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR},
	    nullptr, nullptr);
	ts_fill_gaps_varchar.bind_replace = TSFillGapsBindReplace;
	ts_fill_gaps_varchar.named_parameters["table_name"] = LogicalType::VARCHAR;
	ts_fill_gaps_varchar.named_parameters["group_col"] = LogicalType::VARCHAR;
	ts_fill_gaps_varchar.named_parameters["date_col"] = LogicalType::VARCHAR;
	ts_fill_gaps_varchar.named_parameters["value_col"] = LogicalType::VARCHAR;
	ts_fill_gaps_varchar.named_parameters["frequency"] = LogicalType::VARCHAR;
	loader.RegisterFunction(ts_fill_gaps_varchar);

	// INTEGER frequency overload
	TableFunction ts_fill_gaps_integer(
	    "anofox_fcst_ts_fill_gaps",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::INTEGER},
	    nullptr, nullptr);
	ts_fill_gaps_integer.bind_replace = TSFillGapsIntegerBindReplace;
	ts_fill_gaps_integer.named_parameters["table_name"] = LogicalType::VARCHAR;
	ts_fill_gaps_integer.named_parameters["group_col"] = LogicalType::VARCHAR;
	ts_fill_gaps_integer.named_parameters["date_col"] = LogicalType::VARCHAR;
	ts_fill_gaps_integer.named_parameters["value_col"] = LogicalType::VARCHAR;
	ts_fill_gaps_integer.named_parameters["frequency"] = LogicalType::INTEGER;
	loader.RegisterFunction(ts_fill_gaps_integer);

	// TS_FILL_FORWARD: VARCHAR frequency - target_date accepts ANY type (column name or literal value)
	TableFunction ts_fill_forward_varchar("anofox_fcst_ts_fill_forward",
	                                      {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR,
	                                       LogicalType::VARCHAR, LogicalType::ANY, LogicalType::VARCHAR},
	                                      nullptr, nullptr);
	ts_fill_forward_varchar.bind_replace = TSFillForwardVarcharBindReplace;
	ts_fill_forward_varchar.named_parameters["table_name"] = LogicalType::VARCHAR;
	ts_fill_forward_varchar.named_parameters["group_col"] = LogicalType::VARCHAR;
	ts_fill_forward_varchar.named_parameters["date_col"] = LogicalType::VARCHAR;
	ts_fill_forward_varchar.named_parameters["value_col"] = LogicalType::VARCHAR;
	ts_fill_forward_varchar.named_parameters["target_date"] = LogicalType::ANY;
	ts_fill_forward_varchar.named_parameters["frequency"] = LogicalType::VARCHAR;
	loader.RegisterFunction(ts_fill_forward_varchar);

	// TS_FILL_FORWARD: INTEGER frequency - target_date is INTEGER for INTEGER date columns
	TableFunction ts_fill_forward_integer("anofox_fcst_ts_fill_forward",
	                                      {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR,
	                                       LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::INTEGER},
	                                      nullptr, nullptr);
	ts_fill_forward_integer.bind_replace = TSFillForwardIntegerBindReplace;
	ts_fill_forward_integer.named_parameters["table_name"] = LogicalType::VARCHAR;
	ts_fill_forward_integer.named_parameters["group_col"] = LogicalType::VARCHAR;
	ts_fill_forward_integer.named_parameters["date_col"] = LogicalType::VARCHAR;
	ts_fill_forward_integer.named_parameters["value_col"] = LogicalType::VARCHAR;
	ts_fill_forward_integer.named_parameters["target_date"] = LogicalType::INTEGER;
	ts_fill_forward_integer.named_parameters["frequency"] = LogicalType::INTEGER;
	loader.RegisterFunction(ts_fill_forward_integer);

	// TS_DROP_CONSTANT: 3 params
	TableFunction drop_constant("anofox_fcst_ts_drop_constant",
	                            {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, nullptr, nullptr);
	drop_constant.bind_replace = TSDropConstantBindReplace;
	drop_constant.named_parameters["table_name"] = LogicalType::VARCHAR;
	drop_constant.named_parameters["group_col"] = LogicalType::VARCHAR;
	drop_constant.named_parameters["value_col"] = LogicalType::VARCHAR;
	loader.RegisterFunction(drop_constant);

	// TS_DROP_SHORT: 3 params - min_length is INTEGER
	TableFunction drop_short("anofox_fcst_ts_drop_short",
	                         {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::INTEGER}, nullptr, nullptr);
	drop_short.bind_replace = TSDropShortBindReplace;
	drop_short.named_parameters["table_name"] = LogicalType::VARCHAR;
	drop_short.named_parameters["group_col"] = LogicalType::VARCHAR;
	drop_short.named_parameters["min_length"] = LogicalType::INTEGER;
	loader.RegisterFunction(drop_short);

	// TS_DROP_ZEROS: 3 params
	TableFunction drop_zeros("anofox_fcst_ts_drop_zeros",
	                         {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, nullptr, nullptr);
	drop_zeros.bind_replace = TSDropZerosBindReplace;
	drop_zeros.named_parameters["table_name"] = LogicalType::VARCHAR;
	drop_zeros.named_parameters["group_col"] = LogicalType::VARCHAR;
	drop_zeros.named_parameters["value_col"] = LogicalType::VARCHAR;
	loader.RegisterFunction(drop_zeros);

	// TS_DROP_LEADING_ZEROS: 4 params
	TableFunction drop_leading_zeros(
	    "anofox_fcst_ts_drop_leading_zeros",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, nullptr, nullptr);
	drop_leading_zeros.bind_replace = TSDropLeadingZerosBindReplace;
	drop_leading_zeros.named_parameters["table_name"] = LogicalType::VARCHAR;
	drop_leading_zeros.named_parameters["group_col"] = LogicalType::VARCHAR;
	drop_leading_zeros.named_parameters["date_col"] = LogicalType::VARCHAR;
	drop_leading_zeros.named_parameters["value_col"] = LogicalType::VARCHAR;
	loader.RegisterFunction(drop_leading_zeros);

	// TS_DROP_TRAILING_ZEROS: 4 params
	TableFunction drop_trailing_zeros(
	    "anofox_fcst_ts_drop_trailing_zeros",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, nullptr, nullptr);
	drop_trailing_zeros.bind_replace = TSDropTrailingZerosBindReplace;
	drop_trailing_zeros.named_parameters["table_name"] = LogicalType::VARCHAR;
	drop_trailing_zeros.named_parameters["group_col"] = LogicalType::VARCHAR;
	drop_trailing_zeros.named_parameters["date_col"] = LogicalType::VARCHAR;
	drop_trailing_zeros.named_parameters["value_col"] = LogicalType::VARCHAR;
	loader.RegisterFunction(drop_trailing_zeros);

	// TS_DROP_GAPPY: 4 params - max_gap_pct is DOUBLE
	TableFunction drop_gappy("anofox_fcst_ts_drop_gappy",
	                         {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::DOUBLE},
	                         nullptr, nullptr);
	drop_gappy.bind_replace = TSDropGappyBindReplace;
	drop_gappy.named_parameters["table_name"] = LogicalType::VARCHAR;
	drop_gappy.named_parameters["group_col"] = LogicalType::VARCHAR;
	drop_gappy.named_parameters["date_col"] = LogicalType::VARCHAR;
	drop_gappy.named_parameters["max_gap_pct"] = LogicalType::DOUBLE;
	loader.RegisterFunction(drop_gappy);

	// TS_DROP_EDGE_ZEROS: 4 params
	TableFunction drop_edge_zeros(
	    "anofox_fcst_ts_drop_edge_zeros",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR}, nullptr, nullptr);
	drop_edge_zeros.bind_replace = TSDropEdgeZerosBindReplace;
	drop_edge_zeros.named_parameters["table_name"] = LogicalType::VARCHAR;
	drop_edge_zeros.named_parameters["group_col"] = LogicalType::VARCHAR;
	drop_edge_zeros.named_parameters["date_col"] = LogicalType::VARCHAR;
	drop_edge_zeros.named_parameters["value_col"] = LogicalType::VARCHAR;
	loader.RegisterFunction(drop_edge_zeros);

	// TS_DIFF: 5 params - order is INTEGER
	TableFunction ts_diff(
	    "anofox_fcst_ts_diff",
	    {LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::INTEGER},
	    nullptr, nullptr);
	ts_diff.bind_replace = TSDiffBindReplace;
	ts_diff.named_parameters["table_name"] = LogicalType::VARCHAR;
	ts_diff.named_parameters["group_col"] = LogicalType::VARCHAR;
	ts_diff.named_parameters["date_col"] = LogicalType::VARCHAR;
	ts_diff.named_parameters["value_col"] = LogicalType::VARCHAR;
	ts_diff.named_parameters["order"] = LogicalType::INTEGER;
	loader.RegisterFunction(ts_diff);

	// Register aliases for all TableFunction objects (remove "anofox_fcst_" prefix)
	// Helper lambda to register alias using TableFunctionSet pattern
	auto register_table_alias = [&loader](TableFunction func) {
		string name = func.name;
		if (name.find("anofox_fcst_") == 0) {
			string alias_name = name.substr(12); // Remove "anofox_fcst_" prefix
			TableFunction alias_func = func;
			alias_func.name = alias_name;
			TableFunctionSet alias_set(alias_name);
			alias_set.AddFunction(std::move(alias_func));
			CreateTableFunctionInfo alias_info(std::move(alias_set));
			alias_info.alias_of = name;
			alias_info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
			loader.RegisterFunction(std::move(alias_info));
		}
	};

	// Register aliases for all bind_replace functions
	register_table_alias(fill_nulls_forward);
	register_table_alias(fill_nulls_backward);
	register_table_alias(fill_nulls_mean);
	register_table_alias(fill_nulls_const);
	register_table_alias(ts_fill_gaps_varchar);
	register_table_alias(ts_fill_gaps_integer);
	register_table_alias(ts_fill_forward_varchar);
	register_table_alias(ts_fill_forward_integer);
	register_table_alias(drop_constant);
	register_table_alias(drop_short);
	register_table_alias(drop_zeros);
	register_table_alias(drop_leading_zeros);
	register_table_alias(drop_trailing_zeros);
	register_table_alias(drop_gappy);
	register_table_alias(drop_edge_zeros);
	register_table_alias(ts_diff);

	// Group macros by name to handle overloads
	// Skip macros that are already registered with bind_replace
	std::set<string> bind_replace_macros = {"anofox_fcst_ts_fill_nulls_forward",  "anofox_fcst_ts_fill_nulls_backward",
	                                        "anofox_fcst_ts_fill_nulls_mean",     "anofox_fcst_ts_fill_nulls_const",
	                                        "anofox_fcst_ts_fill_gaps",           "anofox_fcst_ts_fill_forward",
	                                        "anofox_fcst_ts_drop_constant",       "anofox_fcst_ts_drop_short",
	                                        "anofox_fcst_ts_drop_zeros",          "anofox_fcst_ts_drop_leading_zeros",
	                                        "anofox_fcst_ts_drop_trailing_zeros", "anofox_fcst_ts_drop_gappy",
	                                        "anofox_fcst_ts_drop_edge_zeros",     "anofox_fcst_ts_diff"};

	std::map<string, vector<idx_t>> macro_groups;
	for (idx_t index = 0; data_prep_macros[index].name != nullptr; index++) {
		string name = string(data_prep_macros[index].name);
		// Skip macros that use bind_replace
		if (bind_replace_macros.find(name) != bind_replace_macros.end()) {
			continue;
		}
		macro_groups[name].push_back(index);
	}

	// Register each group (handles overloads)
	for (const auto &group : macro_groups) {
		if (group.second.size() == 1) {
			// Single macro, register normally
			idx_t index = group.second[0];
			auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(data_prep_macros[index]);
			table_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
			loader.RegisterFunction(*table_info);

			// Register alias
			if (table_info->name.find("anofox_fcst_") == 0) {
				string alias_name = table_info->name.substr(12); // Remove "anofox_fcst_" prefix
				DefaultTableMacro alias_macro = data_prep_macros[index];
				alias_macro.name = alias_name.c_str();
				auto alias_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(alias_macro);
				alias_info->alias_of = table_info->name;
				alias_info->on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
				loader.RegisterFunction(*alias_info);
			}
		} else {
			// Multiple macros with same name - these should all be skipped as they use bind_replace
			// Skip registration since bind_replace versions are registered above
			continue;
		}
	}
}

} // namespace duckdb
