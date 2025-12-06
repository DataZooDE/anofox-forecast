-- Example: Multiple Seasonal-Trend Decomposition using Loess (MSTL)
-- This script demonstrates how to decompose a time series with two distinct seasonal patterns.

-- Load the extension (Adjust path as needed for your environment)
-- LOAD 'anofox_forecast'; 
-- Or if building locally:
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-------------------------------------------------------------------------------
-- 1. Generate Synthetic Data
-------------------------------------------------------------------------------
-- We create a daily time series spanning one year with:
-- - Trend: Linear growth
-- - Seasonality 1: Weekly pattern (period = 7 days)
-- - Seasonality 2: Monthly pattern (period = 30 days)
-- - Noise: Random variations
CREATE OR REPLACE TABLE multi_seasonal_data AS
SELECT 
    ('2023-01-01'::DATE + INTERVAL (t) DAY) AS date_col,
    'series_1' AS group_col,
    -- Components:
    (t * 0.1) +                          -- Trend
    (sin(2 * 3.14159 * t / 7) * 10) +    -- Weekly Seasonality (Amplitude 10)
    (cos(2 * 3.14159 * t / 30) * 20) +   -- Monthly Seasonality (Amplitude 20)
    ((random() - 0.5) * 5)               -- Noise
    AS value_col
FROM generate_series(0, 365) t(t);

-------------------------------------------------------------------------------
-- 2. Apply MSTL Decomposition
-------------------------------------------------------------------------------
-- We specify the known seasonal periods [7, 30] in the params map.
CREATE OR REPLACE TABLE mstl_result AS
SELECT * FROM ts_mstl_decomposition(
    table_name='multi_seasonal_data',
    group_col='group_col',
    date_col='date_col',
    value_col='value_col',
    params={'seasonal_periods': [7, 30]}
);

-------------------------------------------------------------------------------
-- 3. Analyze Results
-------------------------------------------------------------------------------
-- Display the first 20 rows with decomposed components
SELECT 
    date_col, 
    value_col AS original,
    trend, 
    seasonal_7 AS weekly_pattern, 
    seasonal_30 AS monthly_pattern, 
    residual
FROM mstl_result 
ORDER BY date_col
LIMIT 20;

-------------------------------------------------------------------------------
-- 4. Verify Decomposition Quality
-------------------------------------------------------------------------------
-- The sum of components should equal the original signal (MSTL is additive).
SELECT 
    date_col,
    value_col AS original_value,
    (trend + seasonal_7 + seasonal_30 + residual) AS reconstructed_value,
    (value_col - (trend + seasonal_7 + seasonal_30 + residual)) AS error
FROM mstl_result
ORDER BY date_col
LIMIT 10;

-- Calculate overall reconstruction error metrics
SELECT 
    AVG(ABS(value_col - (trend + seasonal_7 + seasonal_30 + residual))) AS mean_absolute_reconstruction_error,
    MAX(ABS(value_col - (trend + seasonal_7 + seasonal_30 + residual))) AS max_reconstruction_error
FROM mstl_result;

