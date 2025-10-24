-- ============================================================================
-- Anofox Forecast Extension - Demo Script
-- ============================================================================

-- Load the extension
LOAD 'build/debug/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- ============================================================================
-- Example 1: Simple Daily Sales Forecast
-- ============================================================================

CREATE TABLE daily_sales (
    date TIMESTAMP,
    sales DOUBLE
);

INSERT INTO daily_sales VALUES
    ('2024-01-01 00:00:00', 100.0),
    ('2024-01-02 00:00:00', 105.0),
    ('2024-01-03 00:00:00', 102.0),
    ('2024-01-04 00:00:00', 108.0),
    ('2024-01-05 00:00:00', 112.0),
    ('2024-01-06 00:00:00', 115.0),
    ('2024-01-07 00:00:00', 118.0),
    ('2024-01-08 00:00:00', 120.0),
    ('2024-01-09 00:00:00', 122.0),
    ('2024-01-10 00:00:00', 125.0);

-- Generate 7-day forecast using Naive method
SELECT 
    forecast_step,
    ROUND(point_forecast, 2) as forecast,
    ROUND(lower_95, 2) as lower_bound,
    ROUND(upper_95, 2) as upper_bound,
    model_name
FROM FORECAST('date', 'sales', 'Naive', 7, NULL)
ORDER BY forecast_step;

-- ============================================================================
-- Example 2: Compare Different Models
-- ============================================================================

-- Create comparison of Naive vs SMA
WITH naive_forecast AS (
    SELECT 
        forecast_step,
        ROUND(point_forecast, 2) as naive_pred
    FROM FORECAST('date', 'sales', 'Naive', 5, NULL)
),
sma_forecast AS (
    SELECT 
        forecast_step,
        ROUND(point_forecast, 2) as sma_pred
    FROM FORECAST('date', 'sales', 'SMA', 5, NULL)
)
SELECT 
    n.forecast_step,
    n.naive_pred,
    s.sma_pred,
    ROUND(ABS(n.naive_pred - s.sma_pred), 2) as abs_difference,
    CASE 
        WHEN n.naive_pred > s.sma_pred THEN 'Naive higher'
        WHEN n.naive_pred < s.sma_pred THEN 'SMA higher'
        ELSE 'Equal'
    END as comparison
FROM naive_forecast n
JOIN sma_forecast s USING (forecast_step)
ORDER BY forecast_step;

-- ============================================================================
-- Example 3: Forecast with Date Projection
-- ============================================================================

-- Project forecast dates into the future
WITH last_date AS (
    SELECT MAX(date) as last_obs_date FROM daily_sales
),
forecasts AS (
    SELECT 
        forecast_step,
        ROUND(point_forecast, 2) as predicted_sales
    FROM FORECAST('date', 'sales', 'Naive', 10, NULL)
)
SELECT 
    forecast_step,
    DATE_ADD(l.last_obs_date, INTERVAL forecast_step DAY) as forecast_date,
    predicted_sales
FROM forecasts f
CROSS JOIN last_date l
ORDER BY forecast_step;

-- ============================================================================
-- Example 4: Model Performance Metrics
-- ============================================================================

-- Check how long each model takes to fit
SELECT 
    'Naive' as model,
    ROUND(AVG(fit_time_ms), 3) as avg_fit_time_ms
FROM FORECAST('date', 'sales', 'Naive', 5, NULL)
UNION ALL
SELECT 
    'SMA' as model,
    ROUND(AVG(fit_time_ms), 3) as avg_fit_time_ms
FROM FORECAST('date', 'sales', 'SMA', 5, NULL)
ORDER BY avg_fit_time_ms;

-- ============================================================================
-- Example 5: Confidence Interval Analysis
-- ============================================================================

SELECT 
    forecast_step,
    ROUND(point_forecast, 2) as forecast,
    ROUND(lower_95, 2) as lower,
    ROUND(upper_95, 2) as upper,
    ROUND(upper_95 - lower_95, 2) as interval_width,
    ROUND((upper_95 - lower_95) / point_forecast * 100, 1) as interval_pct
FROM FORECAST('date', 'sales', 'SMA', 7, NULL)
ORDER BY forecast_step;

-- ============================================================================
-- Example 6: Multiple Horizons Comparison
-- ============================================================================

-- Compare short vs long term forecasts
WITH short_term AS (
    SELECT 1 as id, forecast_step, point_forecast
    FROM FORECAST('date', 'sales', 'Naive', 3, NULL)
),
long_term AS (
    SELECT 1 as id, forecast_step, point_forecast
    FROM FORECAST('date', 'sales', 'Naive', 10, NULL)
)
SELECT 
    s.forecast_step,
    ROUND(s.point_forecast, 2) as short_term_forecast,
    ROUND(l.point_forecast, 2) as same_in_long_term,
    CASE 
        WHEN ROUND(s.point_forecast, 2) = ROUND(l.point_forecast, 2) 
        THEN 'Consistent' 
        ELSE 'Different' 
    END as consistency
FROM short_term s
JOIN long_term l ON s.forecast_step = l.forecast_step
ORDER BY s.forecast_step;

-- ============================================================================
-- Cleanup
-- ============================================================================

DROP TABLE daily_sales;

-- ============================================================================
-- Summary
-- ============================================================================

-- To see debug output, run this script with:
-- duckdb < DEMO.sql 2>&1 | less

-- All examples above demonstrate working functionality!
-- ============================================================================
