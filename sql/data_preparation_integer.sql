-- ============================================================================
-- Data Preparation for Time Series with INTEGER Date Columns
-- ============================================================================
-- INTEGER-specific versions of data prep macros that use date arithmetic
-- For functions that don't use date arithmetic, the standard versions work fine

-- Load extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- ============================================================================
-- SECTION 1: Fill Time Gaps (INTEGER version)
-- ============================================================================

-- Macro: Fill missing integer gaps in series
CREATE OR REPLACE MACRO TS_FILL_GAPS_INT(table_name, group_cols, date_col, value_col) AS TABLE (
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
            UNNEST(GENERATE_SERIES(min_date, max_date, 1)) AS date_col
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
        date_col,
        value_col
    FROM filled
    ORDER BY group_cols, date_col
);

-- ============================================================================
-- SECTION 2: Fill Forward to Integer
-- ============================================================================

-- Macro: Extend series forward to a specific integer value
CREATE OR REPLACE MACRO TS_FILL_FORWARD_INT(table_name, group_cols, date_col, value_col, target_value) AS TABLE (
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
            UNNEST(GENERATE_SERIES(max_date + 1, target_value, 1)) AS date_col
        FROM max_dates
        WHERE max_date < target_value
    ),
    new_rows AS (
        SELECT
            series_id AS group_cols,
            date_col,
            NULL AS value_col
        FROM extended_dates
    )
    SELECT * FROM series_data
    UNION ALL
    SELECT * FROM new_rows
    ORDER BY group_cols, date_col
);

-- ============================================================================
-- SECTION 3: Drop Gappy Series (INTEGER version)
-- ============================================================================

-- Macro: Drop series with excessive gaps (INTEGER version)
CREATE OR REPLACE MACRO TS_DROP_GAPPY_INT(table_name, group_cols, date_col, max_gap_pct) AS TABLE (
    WITH series_data AS (
        SELECT * FROM QUERY_TABLE(table_name)
    ),
    series_gaps AS (
        SELECT
            group_cols AS series_id,
            COUNT(*) AS actual_length,
            (MAX(date_col) - MIN(date_col)) + 1 AS expected_length,
            1.0 - (CAST(COUNT(*) AS DOUBLE) / ((MAX(date_col) - MIN(date_col)) + 1)) AS gap_ratio
        FROM series_data
        GROUP BY group_cols
        HAVING gap_ratio <= max_gap_pct
    )
    SELECT s.*
    FROM series_data s
    INNER JOIN series_gaps g
        ON s.group_cols = g.series_id
);

-- ============================================================================
-- SECTION 4: Interpolate (INTEGER version)
-- ============================================================================

-- Macro: Linear interpolation for missing values (INTEGER version)
CREATE OR REPLACE MACRO TS_FILL_NULLS_INTERPOLATE_INT(table_name, group_cols, date_col, value_col) AS TABLE (
    WITH series_data AS (
        SELECT * FROM QUERY_TABLE(table_name)
        ORDER BY group_cols, date_col
    ),
    with_neighbors AS (
        SELECT
            group_cols,
            date_col,
            value_col,
            LAST_VALUE(value_col IGNORE NULLS)
                OVER (PARTITION BY group_cols ORDER BY date_col
                      ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) AS prev_val,
            LAST_VALUE(date_col) FILTER (WHERE value_col IS NOT NULL)
                OVER (PARTITION BY group_cols ORDER BY date_col
                      ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) AS prev_date,
            FIRST_VALUE(value_col IGNORE NULLS)
                OVER (PARTITION BY group_cols ORDER BY date_col
                      ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING) AS next_val,
            FIRST_VALUE(date_col) FILTER (WHERE value_col IS NOT NULL)
                OVER (PARTITION BY group_cols ORDER BY date_col
                      ROWS BETWEEN CURRENT ROW AND UNBOUNDED FOLLOWING) AS next_date
        FROM series_data
    )
    SELECT
        group_cols,
        date_col,
        CASE
            WHEN value_col IS NOT NULL THEN value_col
            WHEN prev_val IS NOT NULL AND next_val IS NOT NULL THEN
                prev_val + (next_val - prev_val) *
                    (CAST(date_col - prev_date AS DOUBLE) /
                     NULLIF(next_date - prev_date, 0))
            WHEN prev_val IS NOT NULL THEN prev_val
            WHEN next_val IS NOT NULL THEN next_val
            ELSE 0.0
        END AS value_col
    FROM with_neighbors
);

-- ============================================================================
-- Note: The following macros work with both INTEGER and DATE/TIMESTAMP types
-- because they don't perform date arithmetic:
--
-- - TS_FILL_NULLS_FORWARD
-- - TS_FILL_NULLS_BACKWARD
-- - TS_FILL_NULLS_MEAN
-- - TS_FILL_NULLS_CONST
-- - TS_DROP_CONSTANT
-- - TS_DROP_ZEROS
-- - TS_DROP_SHORT
-- - TS_DROP_LEADING_ZEROS
-- - TS_DROP_TRAILING_ZEROS
-- - TS_DROP_EDGE_ZEROS
--
-- Use the standard versions from data_preparation.sql for these functions.
-- ============================================================================
