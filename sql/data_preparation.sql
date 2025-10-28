-- ============================================================================
-- Data Preparation for Time Series
-- ============================================================================
-- Comprehensive data cleaning and preparation operations for time series
-- Uses DuckDB's powerful SQL capabilities

-- Load extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- ============================================================================
-- SECTION 1: Fill Time Gaps
-- ============================================================================

-- Macro: Fill missing time gaps in series (assumes daily frequency)
CREATE OR REPLACE MACRO TS_FILL_GAPS(table_name, group_cols, date_col, value_col) AS TABLE (
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
);

-- ============================================================================
-- SECTION 2: Fill Forward to Date
-- ============================================================================

-- Macro: Extend series forward to a specific date
CREATE OR REPLACE MACRO TS_FILL_FORWARD(table_name, group_cols, date_col, value_col, target_date) AS TABLE (
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
);

-- ============================================================================
-- SECTION 3: Drop Series by Criteria
-- ============================================================================

-- Macro: Drop constant series
CREATE OR REPLACE MACRO TS_DROP_CONSTANT(table_name, group_cols, value_col) AS TABLE (
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
);

-- Macro: Drop short series
CREATE OR REPLACE MACRO TS_DROP_SHORT(table_name, group_cols, date_col, min_length) AS TABLE (
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
);

-- Macro: Drop series with excessive gaps
CREATE OR REPLACE MACRO TS_DROP_GAPPY(table_name, group_cols, date_col, max_gap_pct) AS TABLE (
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
);

-- ============================================================================
-- SECTION 4: Remove Leading/Trailing Zeros
-- ============================================================================

-- Macro: Drop leading zeros
CREATE OR REPLACE MACRO TS_DROP_LEADING_ZEROS(table_name, group_cols, date_col, value_col) AS TABLE (
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
);

-- Macro: Drop trailing zeros
CREATE OR REPLACE MACRO TS_DROP_TRAILING_ZEROS(table_name, group_cols, date_col, value_col) AS TABLE (
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
);

-- Macro: Drop both leading and trailing zeros
CREATE OR REPLACE MACRO TS_DROP_EDGE_ZEROS(table_name, group_cols, date_col, value_col) AS TABLE (
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
);

-- ============================================================================
-- SECTION 5: Fill Missing Values
-- ============================================================================

-- Macro: Fill missing values with constant
CREATE OR REPLACE MACRO TS_FILL_NULLS_CONST(table_name, group_cols, date_col, value_col, fill_value) AS TABLE (
    SELECT 
        group_cols,
        date_col,
        COALESCE(value_col, fill_value) AS value_col
    FROM QUERY_TABLE(table_name)
);

-- Macro: Forward fill (carry last observation forward)
CREATE OR REPLACE MACRO TS_FILL_NULLS_FORWARD(table_name, group_cols, date_col, value_col) AS TABLE (
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
);

-- Macro: Backward fill (carry next observation backward)
CREATE OR REPLACE MACRO TS_FILL_NULLS_BACKWARD(table_name, group_cols, date_col, value_col) AS TABLE (
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
);

-- Macro: Linear interpolation for missing values
CREATE OR REPLACE MACRO TS_FILL_NULLS_INTERPOLATE(table_name, group_cols, date_col, value_col) AS TABLE (
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
                    (DATE_DIFF('day', prev_date, date_col) / 
                     NULLIF(DATE_DIFF('day', prev_date, next_date), 0))
            WHEN prev_val IS NOT NULL THEN prev_val
            WHEN next_val IS NOT NULL THEN next_val
            ELSE 0.0
        END AS value_col
    FROM with_neighbors
);

-- Macro: Fill with series mean
CREATE OR REPLACE MACRO TS_FILL_NULLS_MEAN(table_name, group_cols, date_col, value_col) AS TABLE (
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
);


-- ============================================================================
-- SECTION 8: Pipeline Macro (Combine Operations)
-- ============================================================================

-- Macro: Standard preparation pipeline
CREATE OR REPLACE MACRO TS_PREPARE_STANDARD(
    table_name, 
    group_cols, 
    date_col, 
    value_col,
    min_length,
    fill_method  -- 'forward', 'mean', 'interpolate', or 'zero'
) AS TABLE (
    WITH 
    -- Step 1: Fill time gaps
    step1 AS (
        SELECT * FROM TS_FILL_GAPS(table_name, group_cols, date_col, value_col)
    ),
    -- Step 2: Drop constant series
    step2 AS (
        SELECT * FROM TS_DROP_CONSTANT('step1', group_cols, value_col)
    ),
    -- Step 3: Drop short series
    step3 AS (
        SELECT * FROM TS_DROP_SHORT('step2', group_cols, date_col, min_length)
    ),
    -- Step 4: Drop leading zeros
    step4 AS (
        SELECT * FROM TS_DROP_LEADING_ZEROS('step3', group_cols, date_col, value_col)
    ),
    -- Step 5: Fill remaining nulls
    step5 AS (
        SELECT * FROM 
        CASE fill_method
            WHEN 'forward' THEN TS_FILL_NULLS_FORWARD('step4', group_cols, date_col, value_col)
            WHEN 'mean' THEN TS_FILL_NULLS_MEAN('step4', group_cols, date_col, value_col)
            WHEN 'interpolate' THEN TS_FILL_NULLS_INTERPOLATE('step4', group_cols, date_col, value_col)
            ELSE TS_FILL_NULLS_CONST('step4', group_cols, date_col, value_col, 0.0)
        END
    )
    SELECT * FROM step5
    ORDER BY group_cols, date_col
);

-- ============================================================================
-- End of Data Preparation Macros
-- ============================================================================

SELECT '✅ Data preparation macros loaded successfully!' AS status;
SELECT 'Use TS_FILL_GAPS(), TS_DROP_CONSTANT(), TS_FILL_NULLS_*(), etc.' AS info;

