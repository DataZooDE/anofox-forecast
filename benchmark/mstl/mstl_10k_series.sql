-- Example: MSTL Decomposition for 10,000 Time Series
-- This script demonstrates MSTL decomposition applied to a large number of series,
-- showing the scalability of the implementation.
--
-- Uses ts_mstl_decomposition_by() table macro which returns array-based output:
--   id, trend[], seasonal[][], remainder[], periods[]

-- Load the extension (Adjust path as needed for your environment)
-- LOAD 'anofox_forecast';
-- Or if building locally:
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Required for CREATE TABLE AS SELECT from table-in-out functions at scale.
-- DuckDB's PhysicalBatchInsert requires batch index support which table-in-out
-- functions don't provide. This setting uses regular parallel insert instead.
SET preserve_insertion_order = false;

-------------------------------------------------------------------------------
-- 1. Generate 10,000 Time Series
-------------------------------------------------------------------------------
-- Each series has:
-- - Unique series ID (0 to 9999)
-- - Daily data for ~600 days (ensures sufficient data for MSTL with period 30)
-- - Trend: Varies by series
-- - Seasonality 1: Weekly pattern (period = 7 days)
-- - Seasonality 2: Monthly pattern (period = 30 days)
-- - Noise: Random variations
-- Note: MSTL requires at least 2 * min(seasonal_periods) observations.
--       For period 30, we need at least 60, but use 600 for reliable decomposition.
CREATE OR REPLACE TABLE multi_series_data AS
SELECT
    ('2023-01-01'::DATE + INTERVAL (t) DAY) AS date_col,
    'series_' || series_id::VARCHAR AS group_col,
    -- Components with series-specific variations:
    (series_id * 0.01 + t * 0.1) +                          -- Trend (varies by series)
    (sin(2 * 3.14159 * t / 7) * (10 + series_id % 5)) +     -- Weekly Seasonality (varies by series)
    (cos(2 * 3.14159 * t / 30) * (20 + series_id % 10)) +   -- Monthly Seasonality (varies by series)
    ((random() - 0.5) * 5)                                   -- Noise
    AS value_col
FROM generate_series(0, 9999) series(series_id)
CROSS JOIN generate_series(0, 599) t(t);  -- 600 days (0-599)

-- Show summary of generated data
SELECT
    COUNT(DISTINCT group_col) AS total_series,
    COUNT(*) AS total_rows,
    MIN(date_col) AS start_date,
    MAX(date_col) AS end_date,
    AVG(value_col) AS avg_value,
    STDDEV(value_col) AS stddev_value
FROM multi_series_data;

-- Verify data quality: Check that each series has sufficient observations
-- MSTL requires at least 2 * min(seasonal_periods) observations per series
SELECT
    group_col,
    COUNT(*) AS n_observations,
    COUNT(DISTINCT date_col) AS n_unique_dates,
    COUNT(*) FILTER (WHERE value_col IS NULL) AS n_nulls
FROM multi_series_data
GROUP BY group_col
HAVING COUNT(*) < 60  -- Minimum required for period 30
ORDER BY n_observations
LIMIT 10;
-- Should return 0 rows if all series have sufficient data

-------------------------------------------------------------------------------
-- 2. Apply MSTL Decomposition to All 10,000 Series
-------------------------------------------------------------------------------
-- Uses ts_mstl_decomposition_by() which returns one row per series with arrays:
--   id (VARCHAR), trend (DOUBLE[]), seasonal (DOUBLE[][]), remainder (DOUBLE[]), periods (INT32[])
CREATE OR REPLACE TABLE mstl_result_10k AS
SELECT * FROM ts_mstl_decomposition_by(
    multi_series_data, group_col, date_col, value_col,
    MAP{'seasonal_periods': [7, 30]}
);

-------------------------------------------------------------------------------
-- 3. Verify Results
-------------------------------------------------------------------------------
-- Check that all series were processed
SELECT
    COUNT(*) AS series_processed,
    AVG(len(trend)) AS avg_points_per_series
FROM mstl_result_10k;

-- Sample a few series: show array lengths and first few values
SELECT
    id,
    len(trend) AS n_points,
    len(seasonal) AS n_seasonal_components,
    periods,
    trend[1:3] AS trend_first3,
    remainder[1:3] AS remainder_first3
FROM mstl_result_10k
WHERE id IN ('series_0', 'series_100', 'series_1000', 'series_5000', 'series_9999')
ORDER BY id;

-------------------------------------------------------------------------------
-- 4. Aggregate Statistics Across All Series
-------------------------------------------------------------------------------
-- Calculate reconstruction error and component strength per series
-- by unnesting arrays for detailed analysis
CREATE OR REPLACE TABLE series_quality_stats AS
WITH unnested AS (
    SELECT
        m.id AS group_col,
        UNNEST(m.trend) AS trend_val,
        UNNEST(m.remainder) AS remainder_val
    FROM mstl_result_10k m
)
SELECT
    group_col,
    COUNT(*) AS n_points,
    AVG(ABS(remainder_val)) AS mean_abs_remainder,
    MAX(ABS(remainder_val)) AS max_remainder,
    STDDEV(trend_val) AS trend_stddev,
    STDDEV(remainder_val) AS residual_stddev
FROM unnested
GROUP BY group_col;

-- Summary statistics across all series
SELECT
    COUNT(*) AS total_series,
    AVG(mean_abs_remainder) AS avg_mean_abs_remainder,
    MAX(max_remainder) AS worst_max_remainder,
    AVG(residual_stddev) AS avg_residual_stddev
FROM series_quality_stats;

-- Distribution of remainder magnitude
SELECT
    CASE
        WHEN mean_abs_remainder < 0.5 THEN '< 0.5'
        WHEN mean_abs_remainder < 1.0 THEN '0.5 - 1.0'
        WHEN mean_abs_remainder < 2.0 THEN '1.0 - 2.0'
        WHEN mean_abs_remainder < 5.0 THEN '2.0 - 5.0'
        ELSE '>= 5.0'
    END AS remainder_range,
    COUNT(*) AS series_count,
    ROUND(100.0 * COUNT(*) / SUM(COUNT(*)) OVER (), 2) AS percentage
FROM series_quality_stats
GROUP BY remainder_range
ORDER BY MIN(mean_abs_remainder);

-------------------------------------------------------------------------------
-- 5. Component Analysis
-------------------------------------------------------------------------------
-- Analyze the strength of each component across all series
SELECT
    'Trend' AS component,
    AVG(trend_stddev) AS avg_stddev
FROM series_quality_stats
UNION ALL
SELECT
    'Residual' AS component,
    AVG(residual_stddev) AS avg_stddev
FROM series_quality_stats;

-- Cleanup (optional)
-- DROP TABLE IF EXISTS multi_series_data;
-- DROP TABLE IF EXISTS mstl_result_10k;
-- DROP TABLE IF EXISTS series_quality_stats;
