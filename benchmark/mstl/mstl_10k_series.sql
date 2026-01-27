-- Example: MSTL Decomposition for 10,000 Time Series
-- This script demonstrates MSTL decomposition applied to a large number of series,
-- showing the scalability of the implementation.
--
-- NOTE: IMPLEMENTATION FIX - Vector Type Initialization
-- ======================================================
-- This script previously encountered a DuckDB error when processing very large datasets
-- (10k series × 600 rows = 6M rows) due to missing vector type initialization in the
-- MSTL operator implementation.
--
-- Error (now fixed): "PhysicalBatchInsert::AddCollection error: batch index 9999999999999
--                    is present in multiple collections"
--
-- Root Cause: The MSTL operator was missing explicit vector type initialization
--            (SetVectorType(VectorType::FLAT_VECTOR)) for output columns, which is
--            required for proper batch handling in parallel execution.
--
-- Fix: Added explicit vector type initialization to match ts_fill_gaps implementation.
--      The operator now properly handles large datasets in parallel execution without
--      requiring batching workarounds.

-- Load the extension (Adjust path as needed for your environment)
-- LOAD 'anofox_forecast';
-- Or if building locally:
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

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
-- ts_mstl_decomposition_by returns per-group arrays:
--   id, trend[], seasonal[][], remainder[], periods[]
.timer on

CREATE OR REPLACE TABLE mstl_result_10k AS
SELECT * FROM ts_mstl_decomposition_by(
    multi_series_data,
    group_col,
    date_col,
    value_col,
    {'seasonal_periods': [7, 30]}
);

.timer off

-------------------------------------------------------------------------------
-- 3. Verify Results
-------------------------------------------------------------------------------
-- Check that all series were processed
SELECT
    COUNT(*) AS series_processed,
    AVG(len(trend)) AS avg_observations_per_series
FROM mstl_result_10k;

-- Sample a few series to verify decomposition
SELECT
    id,
    len(trend) AS n_observations,
    periods AS detected_periods,
    trend[1:3] AS trend_first_3,
    remainder[1:3] AS remainder_first_3
FROM mstl_result_10k
WHERE id IN ('series_0', 'series_100', 'series_1000', 'series_5000', 'series_9999')
ORDER BY id;

-------------------------------------------------------------------------------
-- 4. Aggregate Statistics Across All Series
-------------------------------------------------------------------------------
-- Calculate statistics per series from the array-based results
CREATE OR REPLACE TABLE series_quality_stats AS
WITH unnested AS (
    SELECT
        id AS group_col,
        unnest(trend) AS trend_val,
        unnest(seasonal[1]) AS seasonal_7_val,
        unnest(seasonal[2]) AS seasonal_30_val,
        unnest(remainder) AS remainder_val
    FROM mstl_result_10k
)
SELECT
    group_col,
    COUNT(*) AS n_points,
    STDDEV(remainder_val) AS residual_stddev,
    STDDEV(trend_val) AS trend_stddev,
    STDDEV(seasonal_7_val) AS seasonal_7_stddev,
    STDDEV(seasonal_30_val) AS seasonal_30_stddev,
    STDDEV(trend_val + seasonal_7_val + seasonal_30_val + remainder_val) AS orig_stddev,
    STDDEV(remainder_val) / NULLIF(STDDEV(trend_val + seasonal_7_val + seasonal_30_val + remainder_val), 0) AS noise_reduction_ratio
FROM unnested
GROUP BY group_col;

-- Summary statistics across all series
SELECT
    COUNT(*) AS total_series,
    AVG(noise_reduction_ratio) AS avg_noise_reduction_ratio,
    AVG(residual_stddev) AS avg_residual_stddev
FROM series_quality_stats;

-------------------------------------------------------------------------------
-- 5. Component Analysis
-------------------------------------------------------------------------------
-- Analyze the strength of each component across all series
SELECT
    'Trend' AS component,
    AVG(trend_stddev) AS avg_stddev,
    AVG(trend_stddev / NULLIF(orig_stddev, 0)) AS avg_relative_strength
FROM series_quality_stats
UNION ALL
SELECT
    'Seasonal_7' AS component,
    AVG(seasonal_7_stddev) AS avg_stddev,
    AVG(seasonal_7_stddev / NULLIF(orig_stddev, 0)) AS avg_relative_strength
FROM series_quality_stats
UNION ALL
SELECT
    'Seasonal_30' AS component,
    AVG(seasonal_30_stddev) AS avg_stddev,
    AVG(seasonal_30_stddev / NULLIF(orig_stddev, 0)) AS avg_relative_strength
FROM series_quality_stats
UNION ALL
SELECT
    'Residual' AS component,
    AVG(residual_stddev) AS avg_stddev,
    AVG(residual_stddev / NULLIF(orig_stddev, 0)) AS avg_relative_strength
FROM series_quality_stats;

-- Cleanup (optional)
-- DROP TABLE IF EXISTS multi_series_data;
-- DROP TABLE IF EXISTS mstl_result_10k;
-- DROP TABLE IF EXISTS series_quality_stats;
