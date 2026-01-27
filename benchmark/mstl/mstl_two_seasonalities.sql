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
-- ts_mstl_decomposition_by returns: id, trend[], seasonal[][], remainder[], periods[]
CREATE OR REPLACE TABLE mstl_result AS
SELECT * FROM ts_mstl_decomposition_by(
    multi_seasonal_data,
    group_col,
    date_col,
    value_col,
    {'seasonal_periods': [7, 30]}
);

-------------------------------------------------------------------------------
-- 3. Analyze Results
-------------------------------------------------------------------------------
-- The _by function returns arrays; unnest to get per-observation rows.
-- seasonal is a nested array: seasonal[1] = weekly (period 7), seasonal[2] = monthly (period 30)
SELECT
    id,
    len(trend) AS n_observations,
    periods AS detected_periods,
    trend[1:5] AS trend_first_5,
    seasonal[1][1:5] AS weekly_first_5,
    seasonal[2][1:5] AS monthly_first_5,
    remainder[1:5] AS remainder_first_5
FROM mstl_result;

-------------------------------------------------------------------------------
-- 4. Verify Decomposition Quality
-------------------------------------------------------------------------------
-- Check that trend + seasonal components + remainder reconstructs the original.
-- We do this by unnesting and comparing against the original data.
WITH original AS (
    SELECT
        row_number() OVER (ORDER BY date_col) AS idx,
        value_col
    FROM multi_seasonal_data
),
decomposed AS (
    SELECT
        unnest(generate_series(1, len(trend))) AS idx,
        unnest(trend) AS trend_val,
        unnest(seasonal[1]) AS weekly_val,
        unnest(seasonal[2]) AS monthly_val,
        unnest(remainder) AS remainder_val
    FROM mstl_result
)
SELECT
    AVG(ABS(o.value_col - (d.trend_val + d.weekly_val + d.monthly_val + d.remainder_val))) AS mean_absolute_reconstruction_error,
    MAX(ABS(o.value_col - (d.trend_val + d.weekly_val + d.monthly_val + d.remainder_val))) AS max_reconstruction_error
FROM original o
JOIN decomposed d ON o.idx = d.idx;
