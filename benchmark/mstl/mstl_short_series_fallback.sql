-- =============================================================================
-- MSTL Short Series Fallback Test
-- =============================================================================
-- Tests MSTL behavior with series shorter than 2 seasonal periods.
-- MSTL requires at least 2 * min(seasonal_periods) observations.
-- For period=12, minimum is 24. For period=30, minimum is 60.
--
-- Run with: duckdb -unsigned < benchmark/mstl/mstl_short_series_fallback.sql
-- =============================================================================

-- Load the extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Enable timing
.timer on

SELECT '=============================================================================';
SELECT 'MSTL Short Series Fallback Test';
SELECT '=============================================================================';

-- =============================================================================
-- SECTION 1: Generate Test Data with Various Series Lengths
-- =============================================================================
SELECT '';
SELECT '--- Section 1: Generate Test Data ---';

-- Create series with different lengths to test fallback behavior
-- Period = 12, so minimum for full MSTL is 24
CREATE OR REPLACE TABLE short_series_test AS
WITH lengths AS (
    SELECT * FROM (VALUES
        (5),    -- Very short: 5 points (< 2*12 = 24)
        (10),   -- Short: 10 points (< 24)
        (15),   -- Short: 15 points (< 24)
        (20),   -- Short: 20 points (< 24)
        (23),   -- Edge case: 23 points (just under 24)
        (24),   -- Minimum: 24 points (exactly 2*12)
        (25),   -- Just above minimum: 25 points
        (36),   -- 3 seasons
        (48),   -- 4 seasons
        (100)   -- Long series
    ) AS t(series_length)
),
series_data AS (
    SELECT
        'len_' || l.series_length AS group_col,
        l.series_length,
        DATE '2024-01-01' + INTERVAL (t) DAY AS date_col,
        -- Simple pattern: trend + seasonality
        100 + t * 0.5 + 10 * SIN(2 * PI() * t / 12) + (RANDOM() - 0.5) * 2 AS value_col
    FROM lengths l
    CROSS JOIN generate_series(0, 99) AS d(t)
    WHERE t < l.series_length
)
SELECT * FROM series_data;

-- Show data summary
SELECT
    group_col,
    COUNT(*) AS n_points,
    CASE
        WHEN COUNT(*) < 24 THEN 'SHORT (< 2 seasons)'
        WHEN COUNT(*) = 24 THEN 'MINIMUM (= 2 seasons)'
        ELSE 'SUFFICIENT (> 2 seasons)'
    END AS status
FROM short_series_test
GROUP BY group_col
ORDER BY COUNT(*);

-- =============================================================================
-- SECTION 2: MSTL Decomposition with Sufficient Data
-- =============================================================================
SELECT '';
SELECT '--- Section 2: MSTL Decomposition with Sufficient Data ---';

-- Test with sufficient data series only (should work)
CREATE OR REPLACE TABLE sufficient_series AS
SELECT * FROM short_series_test WHERE group_col IN ('len_24', 'len_25', 'len_36', 'len_48', 'len_100');

SELECT 'Testing MSTL with sufficient data series:';
SELECT
    id AS group_col,
    len(trend) AS trend_length,
    len(remainder) AS remainder_length,
    periods AS detected_periods
FROM ts_mstl_decomposition_by(
    sufficient_series,
    group_col,
    date_col,
    value_col,
    {'seasonal_periods': [12]}
)
ORDER BY id;

-- =============================================================================
-- SECTION 3: MSTL Decomposition with All Series (including short)
-- =============================================================================
SELECT '';
SELECT '--- Section 3: MSTL Decomposition with All Series ---';

SELECT 'Testing MSTL with all series (including short):';
SELECT
    id AS group_col,
    len(trend) AS trend_length,
    CASE WHEN len(trend) > 0 THEN 'SUCCESS' ELSE 'NO_DECOMPOSITION' END AS status
FROM ts_mstl_decomposition_by(
    short_series_test,
    group_col,
    date_col,
    value_col,
    {'seasonal_periods': [12]}
)
ORDER BY len(trend);

-- =============================================================================
-- SECTION 4: Edge Cases
-- =============================================================================
SELECT '';
SELECT '--- Section 4: Edge Cases ---';

-- Test exactly at the boundary (24 points for period 12)
SELECT 'Edge case: Exactly 24 points (2 * period):';
CREATE OR REPLACE TABLE edge_case_24 AS
SELECT * FROM short_series_test WHERE group_col = 'len_24';

SELECT
    id,
    trend[1:5] AS trend_first_5,
    remainder[1:5] AS remainder_first_5
FROM ts_mstl_decomposition_by(
    edge_case_24,
    group_col,
    date_col,
    value_col,
    {'seasonal_periods': [12]}
);

-- Test one below boundary (23 points)
SELECT 'Edge case: 23 points (just under 2 * period):';
CREATE OR REPLACE TABLE edge_case_23 AS
SELECT * FROM short_series_test WHERE group_col = 'len_23';

SELECT
    id,
    len(trend) AS trend_length,
    CASE WHEN len(trend) > 0 THEN 'DECOMPOSED' ELSE 'SKIPPED' END AS result
FROM ts_mstl_decomposition_by(
    edge_case_23,
    group_col,
    date_col,
    value_col,
    {'seasonal_periods': [12]}
);

-- =============================================================================
-- SECTION 5: Performance with Mixed-Length Series (Realistic Scenario)
-- =============================================================================
SELECT '';
SELECT '--- Section 5: Performance with Mixed-Length Series (10k series) ---';

-- Generate 10,000 series with varying lengths (realistic scenario)
CREATE OR REPLACE TABLE mixed_length_series AS
WITH series_lengths AS (
    SELECT
        series_id,
        -- Random length between 10 and 200
        10 + (ABS(HASH(series_id)) % 190) AS series_length
    FROM generate_series(1, 10000) AS t(series_id)
)
SELECT
    series_id,
    'series_' || LPAD(l.series_id::VARCHAR, 5, '0') AS group_col,
    DATE '2024-01-01' + INTERVAL (t) DAY AS date_col,
    100 + t * 0.3 + 15 * SIN(2 * PI() * t / 12) + (RANDOM() - 0.5) * 3 AS value_col
FROM series_lengths l
CROSS JOIN generate_series(0, 199) AS d(t)
WHERE t < l.series_length;

-- Show length distribution
SELECT
    CASE
        WHEN n_points < 24 THEN 'SHORT (< 24)'
        WHEN n_points < 50 THEN 'MEDIUM (24-49)'
        ELSE 'LONG (50+)'
    END AS length_category,
    COUNT(*) AS series_count
FROM (
    SELECT group_col, COUNT(*) AS n_points
    FROM mixed_length_series
    GROUP BY group_col
) subq
GROUP BY length_category
ORDER BY length_category;

-- Benchmark: Process 10k mixed-length series
SELECT 'Processing 10k mixed-length series with MSTL:';
SELECT
    COUNT(*) AS series_processed,
    SUM(CASE WHEN len(trend) > 0 THEN 1 ELSE 0 END) AS decomposed_count,
    SUM(CASE WHEN len(trend) = 0 THEN 1 ELSE 0 END) AS skipped_count
FROM ts_mstl_decomposition_by(
    mixed_length_series,
    group_col,
    date_col,
    value_col,
    {'seasonal_periods': [12]}
);

-- =============================================================================
-- SECTION 6: Forecasting Short Series
-- =============================================================================
SELECT '';
SELECT '--- Section 6: Forecasting Short Series ---';

-- Test forecasting with MSTL model on short series
SELECT 'Forecasting short series (len=10, 15, 20) with MSTL:';
CREATE OR REPLACE TABLE forecast_test_short AS
SELECT * FROM short_series_test
WHERE group_col IN ('len_10', 'len_15', 'len_20');

-- MSTL should gracefully handle short series
SELECT
    COUNT(*) AS total_forecast_rows,
    COUNT(DISTINCT id) AS series_count
FROM TS_FORECAST_BY(
    forecast_test_short,
    group_col,
    date_col,
    value_col,
    'MSTL',
    12,
    {'seasonal_periods': [12]}
);

-- Compare with simple Naive model
SELECT 'Forecasting same series with Naive model:';
SELECT
    COUNT(*) AS total_forecast_rows,
    COUNT(DISTINCT id) AS series_count
FROM TS_FORECAST_BY(
    forecast_test_short,
    group_col,
    date_col,
    value_col,
    'Naive',
    12,
    MAP{}
);

-- =============================================================================
-- SECTION 7: Full Comparison - Short vs Sufficient Series
-- =============================================================================
SELECT '';
SELECT '--- Section 7: Full Comparison ---';

-- Create tables for comparison
CREATE OR REPLACE TABLE short_only AS
SELECT * FROM short_series_test WHERE group_col IN ('len_5', 'len_10', 'len_15', 'len_20', 'len_23');

CREATE OR REPLACE TABLE sufficient_only AS
SELECT * FROM short_series_test WHERE group_col IN ('len_24', 'len_25', 'len_36', 'len_48', 'len_100');

SELECT 'Decomposition results - SHORT series (<24 points):';
SELECT
    id,
    len(trend) AS trend_len,
    len(remainder) AS remainder_len,
    CASE WHEN len(trend) > 0 THEN 'OK' ELSE 'SKIPPED' END AS status
FROM ts_mstl_decomposition_by(short_only, group_col, date_col, value_col, {'seasonal_periods': [12]})
ORDER BY id;

SELECT 'Decomposition results - SUFFICIENT series (>=24 points):';
SELECT
    id,
    len(trend) AS trend_len,
    len(remainder) AS remainder_len,
    CASE WHEN len(trend) > 0 THEN 'OK' ELSE 'SKIPPED' END AS status
FROM ts_mstl_decomposition_by(sufficient_only, group_col, date_col, value_col, {'seasonal_periods': [12]})
ORDER BY id;

-- =============================================================================
-- Cleanup
-- =============================================================================
SELECT '';
SELECT '--- Cleanup ---';
DROP TABLE IF EXISTS short_series_test;
DROP TABLE IF EXISTS sufficient_series;
DROP TABLE IF EXISTS mixed_length_series;
DROP TABLE IF EXISTS forecast_test_short;
DROP TABLE IF EXISTS short_only;
DROP TABLE IF EXISTS sufficient_only;
DROP TABLE IF EXISTS edge_case_24;
DROP TABLE IF EXISTS edge_case_23;

SELECT '=============================================================================';
SELECT 'Short Series Fallback Test Complete';
SELECT '=============================================================================';
