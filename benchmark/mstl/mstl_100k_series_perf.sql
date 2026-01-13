-- =============================================================================
-- MSTL/STL Performance Benchmark: 100k Series via SQL Interface
-- =============================================================================
-- This benchmark tests MSTL decomposition and forecasting performance
-- at scale (100,000 time series) through the DuckDB SQL interface.
--
-- Run with: duckdb -unsigned < benchmark/mstl/mstl_100k_series_perf.sql
-- =============================================================================

-- Load the extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Enable timing
.timer on

SELECT '=============================================================================';
SELECT 'MSTL/STL Performance Benchmark: 100k Series';
SELECT '=============================================================================';

-- =============================================================================
-- SECTION 1: Data Generation
-- =============================================================================
SELECT '';
SELECT '--- Section 1: Data Generation ---';

-- Generate 100,000 time series with different characteristics
-- Each series has 100 data points (short but realistic for batch processing)
CREATE OR REPLACE TABLE benchmark_100k AS
WITH series_params AS (
    SELECT
        series_id,
        100 + (series_id % 500) AS base_level,
        (series_id % 100) * 0.01 AS trend_slope,
        10 + (series_id % 20) AS seasonal_amplitude
    FROM generate_series(1, 100000) AS t(series_id)
)
SELECT
    p.series_id,
    'series_' || LPAD(p.series_id::VARCHAR, 6, '0') AS group_col,
    DATE '2024-01-01' + INTERVAL (t) DAY AS date_col,
    -- Generate realistic time series with trend + seasonality + noise
    p.base_level
    + p.trend_slope * t
    + p.seasonal_amplitude * SIN(2 * PI() * t / 12)
    + (RANDOM() - 0.5) * 5 AS value_col
FROM series_params p
CROSS JOIN generate_series(0, 99) AS d(t);

-- Show data summary
SELECT
    COUNT(DISTINCT series_id) AS total_series,
    COUNT(*) AS total_rows,
    COUNT(*) / COUNT(DISTINCT series_id) AS rows_per_series,
    MIN(date_col) AS start_date,
    MAX(date_col) AS end_date
FROM benchmark_100k;

-- =============================================================================
-- SECTION 2: MSTL Decomposition Performance
-- =============================================================================
SELECT '';
SELECT '--- Section 2: MSTL Decomposition (100k series) ---';

-- Benchmark: MSTL decomposition on 100k series (aggregate function)
SELECT 'Starting MSTL decomposition on 100,000 series...';

SELECT
    COUNT(*) AS series_processed,
    SUM(CASE WHEN len(decomposition.trend) > 0 THEN 1 ELSE 0 END) AS with_trend,
    SUM(CASE WHEN len(decomposition.periods) > 0 THEN 1 ELSE 0 END) AS with_seasonal
FROM ts_mstl_decomposition(
    benchmark_100k,
    group_col,
    date_col,
    value_col,
    {'seasonal_periods': [12]}
);

-- =============================================================================
-- SECTION 3: Period Detection Performance
-- =============================================================================
SELECT '';
SELECT '--- Section 3: Period Detection Performance ---';

-- Create a subset for period detection testing
CREATE OR REPLACE TABLE benchmark_1k AS
SELECT * FROM benchmark_100k WHERE series_id <= 1000;

-- Test FFT period detection (fast) using aggregate
SELECT 'Period Detection: FFT (1000 series)';
SELECT COUNT(*) AS series_with_periods
FROM (
    SELECT
        series_id,
        ts_detect_periods(list(value_col ORDER BY date_col), 'fft') AS detected_period
    FROM benchmark_1k
    GROUP BY series_id
);

-- =============================================================================
-- SECTION 4: Forecasting with MSTL Model (Scalability Test)
-- =============================================================================
SELECT '';
SELECT '--- Section 4: Forecasting Performance Test ---';

-- Test 1k series
SELECT 'Forecast: 1k series with MSTL';
SELECT COUNT(*) AS forecast_rows
FROM TS_FORECAST_BY(benchmark_1k, series_id, date_col, value_col, 'MSTL', 12, {'seasonal_periods': [12]});

-- Test 10k series
CREATE OR REPLACE TABLE benchmark_10k AS
SELECT * FROM benchmark_100k WHERE series_id <= 10000;

SELECT 'Forecast: 10k series with MSTL';
SELECT COUNT(*) AS forecast_rows
FROM TS_FORECAST_BY(benchmark_10k, series_id, date_col, value_col, 'MSTL', 12, {'seasonal_periods': [12]});

-- Test 50k series
CREATE OR REPLACE TABLE benchmark_50k AS
SELECT * FROM benchmark_100k WHERE series_id <= 50000;

SELECT 'Forecast: 50k series with MSTL';
SELECT COUNT(*) AS forecast_rows
FROM TS_FORECAST_BY(benchmark_50k, series_id, date_col, value_col, 'MSTL', 12, {'seasonal_periods': [12]});

-- Test 100k series
SELECT 'Forecast: 100k series with MSTL';
SELECT COUNT(*) AS forecast_rows
FROM TS_FORECAST_BY(benchmark_100k, series_id, date_col, value_col, 'MSTL', 12, {'seasonal_periods': [12]});

-- =============================================================================
-- SECTION 5: Model Comparison (10k series)
-- =============================================================================
SELECT '';
SELECT '--- Section 5: Model Performance Comparison (10k series) ---';

SELECT 'Model: Naive (10k series)';
SELECT COUNT(*) AS forecast_rows
FROM TS_FORECAST_BY(benchmark_10k, series_id, date_col, value_col, 'Naive', 12, MAP{});

SELECT 'Model: SES (10k series)';
SELECT COUNT(*) AS forecast_rows
FROM TS_FORECAST_BY(benchmark_10k, series_id, date_col, value_col, 'SES', 12, MAP{});

SELECT 'Model: HoltWinters (10k series)';
SELECT COUNT(*) AS forecast_rows
FROM TS_FORECAST_BY(benchmark_10k, series_id, date_col, value_col, 'HoltWinters', 12, {'seasonal_period': 12});

SELECT 'Model: MSTL (10k series)';
SELECT COUNT(*) AS forecast_rows
FROM TS_FORECAST_BY(benchmark_10k, series_id, date_col, value_col, 'MSTL', 12, {'seasonal_periods': [12]});

SELECT 'Model: AutoMSTL (10k series)';
SELECT COUNT(*) AS forecast_rows
FROM TS_FORECAST_BY(benchmark_10k, series_id, date_col, value_col, 'AutoMSTL', 12, {'seasonal_periods': [12]});

-- =============================================================================
-- SECTION 6: Mixed-Length Series Test (Realistic Scenario)
-- =============================================================================
SELECT '';
SELECT '--- Section 6: Mixed-Length Series (100k with varying lengths) ---';

-- Generate 100k series with realistic length distribution:
-- - 10% very short (5-15 points) - new products, limited history
-- - 15% short (16-23 points) - below MSTL minimum
-- - 25% medium (24-50 points) - just enough for decomposition
-- - 50% long (51-200 points) - established products
CREATE OR REPLACE TABLE benchmark_mixed AS
WITH series_lengths AS (
    SELECT
        series_id,
        CASE
            -- 10% very short (5-15 points)
            WHEN series_id % 100 < 10 THEN 5 + (series_id % 11)
            -- 15% short (16-23 points) - below minimum
            WHEN series_id % 100 < 25 THEN 16 + (series_id % 8)
            -- 25% medium (24-50 points)
            WHEN series_id % 100 < 50 THEN 24 + (series_id % 27)
            -- 50% long (51-200 points)
            ELSE 51 + (series_id % 150)
        END AS series_length
    FROM generate_series(1, 100000) AS t(series_id)
)
SELECT
    l.series_id,
    'series_' || LPAD(l.series_id::VARCHAR, 6, '0') AS group_col,
    DATE '2024-01-01' + INTERVAL (t) DAY AS date_col,
    100 + (l.series_id % 500)
    + (l.series_id % 100) * 0.01 * t
    + (10 + l.series_id % 20) * SIN(2 * PI() * t / 12)
    + (RANDOM() - 0.5) * 5 AS value_col
FROM series_lengths l
CROSS JOIN generate_series(0, 199) AS d(t)
WHERE t < l.series_length;

-- Show length distribution
SELECT 'Series length distribution:';
SELECT
    CASE
        WHEN n_points < 16 THEN '1. Very short (5-15)'
        WHEN n_points < 24 THEN '2. Short (16-23) - below minimum'
        WHEN n_points < 51 THEN '3. Medium (24-50)'
        ELSE '4. Long (51-200)'
    END AS length_category,
    COUNT(*) AS series_count,
    ROUND(100.0 * COUNT(*) / SUM(COUNT(*)) OVER (), 1) AS percentage,
    MIN(n_points) AS min_len,
    MAX(n_points) AS max_len,
    ROUND(AVG(n_points), 1) AS avg_len
FROM (
    SELECT group_col, COUNT(*) AS n_points
    FROM benchmark_mixed
    GROUP BY group_col
) subq
GROUP BY length_category
ORDER BY length_category;

-- Total stats
SELECT
    COUNT(DISTINCT group_col) AS total_series,
    COUNT(*) AS total_rows,
    ROUND(AVG(n_points), 1) AS avg_series_length
FROM (
    SELECT group_col, COUNT(*) AS n_points
    FROM benchmark_mixed
    GROUP BY group_col
) subq;

-- MSTL Decomposition on mixed-length series
SELECT 'MSTL Decomposition on 100k mixed-length series:';
SELECT
    COUNT(*) AS series_processed,
    SUM(CASE WHEN len(decomposition.trend) > 0 THEN 1 ELSE 0 END) AS with_decomposition,
    SUM(CASE WHEN len(decomposition.trend) = 0 THEN 1 ELSE 0 END) AS skipped
FROM ts_mstl_decomposition(
    benchmark_mixed,
    group_col,
    date_col,
    value_col,
    {'seasonal_periods': [12]}
);

-- Forecast on mixed-length series
SELECT 'MSTL Forecast on 100k mixed-length series:';
SELECT COUNT(*) AS forecast_rows
FROM TS_FORECAST_BY(benchmark_mixed, series_id, date_col, value_col, 'MSTL', 12, {'seasonal_periods': [12]});

-- =============================================================================
-- SECTION 7: Longer Series Test
-- =============================================================================
SELECT '';
SELECT '--- Section 7: Longer Series Test (1k series x 500 points) ---';

CREATE OR REPLACE TABLE benchmark_long AS
SELECT
    series_id,
    'series_' || LPAD(series_id::VARCHAR, 4, '0') AS group_col,
    DATE '2024-01-01' + INTERVAL (t) DAY AS date_col,
    100 + t * 0.1 + 15 * SIN(2 * PI() * t / 12) + 10 * SIN(2 * PI() * t / 52) + (RANDOM() - 0.5) * 5 AS value_col
FROM generate_series(1, 1000) AS s(series_id)
CROSS JOIN generate_series(0, 499) AS d(t);

SELECT
    COUNT(DISTINCT series_id) AS series_count,
    COUNT(*) AS total_rows,
    COUNT(*) / COUNT(DISTINCT series_id) AS rows_per_series
FROM benchmark_long;

SELECT 'MSTL Decomposition on longer series (1k x 500 points)';
SELECT
    COUNT(*) AS series_processed
FROM ts_mstl_decomposition(
    benchmark_long,
    group_col,
    date_col,
    value_col,
    {'seasonal_periods': [12, 52]}
);

SELECT 'MSTL Forecast on longer series (1k x 500 points)';
SELECT COUNT(*) AS forecast_rows
FROM TS_FORECAST_BY(benchmark_long, series_id, date_col, value_col, 'MSTL', 12, {'seasonal_periods': [12, 52]});

-- =============================================================================
-- Cleanup
-- =============================================================================
SELECT '';
SELECT '--- Cleanup ---';
DROP TABLE IF EXISTS benchmark_100k;
DROP TABLE IF EXISTS benchmark_50k;
DROP TABLE IF EXISTS benchmark_10k;
DROP TABLE IF EXISTS benchmark_1k;
DROP TABLE IF EXISTS benchmark_mixed;
DROP TABLE IF EXISTS benchmark_long;

SELECT '=============================================================================';
SELECT 'Benchmark Complete';
SELECT '=============================================================================';
