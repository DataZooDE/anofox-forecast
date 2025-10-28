-- ============================================================================
-- ROLLING FORECAST EXAMPLE
-- ============================================================================
-- Demonstrates how to perform rolling/expanding window forecasting
-- for backtesting and model evaluation on 10,000 time series
--
-- Rolling Forecast Strategy:
-- 1. Train on historical data up to cutoff point
-- 2. Generate forecast for next H periods
-- 3. Compare forecast to actual values
-- 4. Roll forward and repeat
--
-- ============================================================================

.timer on

-- ============================================================================
-- STEP 1: Create Synthetic Dataset (10,000 series, 365 days)
-- ============================================================================
SELECT '=== STEP 1: Creating 10k series dataset ===' AS step;

DROP TABLE IF EXISTS rolling_data;
CREATE TABLE rolling_data AS
WITH RECURSIVE
    series_ids AS (
        SELECT 1 AS series_id
        UNION ALL
        SELECT series_id + 1 FROM series_ids WHERE series_id < 10000
    ),
    dates AS (
        SELECT DATE '2023-01-01' + INTERVAL (d) DAY AS ds
        FROM generate_series(0, 364) t(d)
    ),
    base_data AS (
        SELECT 
            series_id,
            ds,
            -- Trend component
            0.1 * series_id * EXTRACT(DAY FROM ds) +
            -- Seasonal component (weekly)
            10 * SIN(2 * PI() * EXTRACT(DAY FROM ds) / 7) +
            -- Seasonal component (monthly)
            5 * COS(2 * PI() * EXTRACT(DAY FROM ds) / 30) +
            -- Base level (varies by series)
            100 + (series_id % 100) * 2 +
            -- Random noise
            (RANDOM() * 10 - 5) AS y
        FROM series_ids, dates
    )
SELECT * FROM base_data;

SELECT COUNT(*) AS total_rows, 
       COUNT(DISTINCT series_id) AS num_series,
       MIN(ds) AS start_date,
       MAX(ds) AS end_date
FROM rolling_data;

-- ============================================================================
-- STEP 2: Define Rolling Forecast Windows
-- ============================================================================
SELECT '=== STEP 2: Rolling forecast with 3 cutoff points ===' AS step;

-- Strategy: Train on data up to cutoff, forecast next 28 days, evaluate
-- Cutoffs: 2023-10-01, 2023-11-01, 2023-12-01

DROP TABLE IF EXISTS rolling_forecasts;
CREATE TABLE rolling_forecasts AS
-- Cutoff 1: Train on Jan-Sep, forecast Oct 1-28
WITH cutoff1 AS (
    SELECT 
        1 AS cutoff_id,
        DATE '2023-10-01' AS cutoff_date,
        series_id,
        forecast_step,
        ds,
        point_forecast,
        lower,
        upper,
        model_name
    FROM TS_FORECAST_BY(
        (SELECT * FROM rolling_data WHERE ds < '2023-10-01'),
        series_id, ds, y, 'AutoETS', 28,
        {'seasonal_period': 7, 'model': 'AAA'}
    )
),
-- Cutoff 2: Train on Jan-Oct, forecast Nov 1-28
cutoff2 AS (
    SELECT 
        2 AS cutoff_id,
        DATE '2023-11-01' AS cutoff_date,
        series_id,
        forecast_step,
        ds,
        point_forecast,
        lower,
        upper,
        model_name
    FROM TS_FORECAST_BY(
        (SELECT * FROM rolling_data WHERE ds < '2023-11-01'),
        series_id, ds, y, 'AutoETS', 28,
        {'seasonal_period': 7, 'model': 'AAA'}
    )
),
-- Cutoff 3: Train on Jan-Nov, forecast Dec 1-28
cutoff3 AS (
    SELECT 
        3 AS cutoff_id,
        DATE '2023-12-01' AS cutoff_date,
        series_id,
        forecast_step,
        ds,
        point_forecast,
        lower,
        upper,
        model_name
    FROM TS_FORECAST_BY(
        (SELECT * FROM rolling_data WHERE ds < '2023-12-01'),
        series_id, ds, y, 'AutoETS', 28,
        {'seasonal_period': 7, 'model': 'AAA'}
    )
)
SELECT * FROM cutoff1
UNION ALL
SELECT * FROM cutoff2
UNION ALL
SELECT * FROM cutoff3;

SELECT 
    cutoff_id,
    cutoff_date,
    COUNT(DISTINCT series_id) AS num_series,
    COUNT(*) AS num_forecasts,
    ROUND(AVG(point_forecast), 2) AS avg_forecast
FROM rolling_forecasts
GROUP BY cutoff_id, cutoff_date
ORDER BY cutoff_id;

-- ============================================================================
-- STEP 3: Join Forecasts with Actuals
-- ============================================================================
SELECT '=== STEP 3: Joining forecasts with actual values ===' AS step;

DROP TABLE IF EXISTS rolling_evaluation;
CREATE TABLE rolling_evaluation AS
SELECT 
    f.cutoff_id,
    f.cutoff_date,
    f.series_id,
    f.forecast_step,
    f.ds,
    f.point_forecast,
    f.lower,
    f.upper,
    a.y AS actual,
    f.point_forecast - a.y AS error,
    ABS(f.point_forecast - a.y) AS abs_error,
    CASE WHEN a.y BETWEEN f.lower AND f.upper THEN 1 ELSE 0 END AS in_interval
FROM rolling_forecasts f
JOIN rolling_data a ON f.series_id = a.series_id AND f.ds = a.ds;

SELECT 
    cutoff_id,
    COUNT(*) AS num_forecasts,
    ROUND(AVG(abs_error), 3) AS mae,
    ROUND(SQRT(AVG(error * error)), 3) AS rmse,
    ROUND(AVG(error), 3) AS bias,
    ROUND(100.0 * AVG(in_interval), 1) || '%' AS coverage
FROM rolling_evaluation
GROUP BY cutoff_id
ORDER BY cutoff_id;

-- ============================================================================
-- STEP 4: Aggregate Metrics Across All Cutoffs
-- ============================================================================
SELECT '=== STEP 4: Overall rolling forecast performance ===' AS step;

SELECT 
    'Overall' AS metric_scope,
    COUNT(*) AS total_forecasts,
    ROUND(AVG(abs_error), 3) AS mae,
    ROUND(SQRT(AVG(error * error)), 3) AS rmse,
    ROUND(AVG(error), 3) AS bias,
    ROUND(100.0 * AVG(in_interval), 1) || '%' AS coverage
FROM rolling_evaluation;

-- ============================================================================
-- STEP 5: Analyze Forecast Performance by Horizon
-- ============================================================================
SELECT '=== STEP 5: Performance by forecast horizon ===' AS step;

SELECT 
    CASE 
        WHEN forecast_step <= 7 THEN '1-7 days'
        WHEN forecast_step <= 14 THEN '8-14 days'
        WHEN forecast_step <= 21 THEN '15-21 days'
        ELSE '22-28 days'
    END AS horizon_group,
    COUNT(*) AS num_forecasts,
    ROUND(AVG(abs_error), 3) AS mae,
    ROUND(SQRT(AVG(error * error)), 3) AS rmse,
    ROUND(100.0 * AVG(in_interval), 1) || '%' AS coverage
FROM rolling_evaluation
GROUP BY horizon_group
ORDER BY MIN(forecast_step);

-- ============================================================================
-- STEP 6: Sample Results - Show Top 5 Series
-- ============================================================================
SELECT '=== STEP 6: Sample forecasts for series 1-5 ===' AS step;

SELECT 
    series_id,
    cutoff_id,
    forecast_step,
    ds,
    ROUND(actual, 2) AS actual,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(abs_error, 2) AS abs_error,
    in_interval
FROM rolling_evaluation
WHERE series_id <= 5 AND forecast_step <= 7
ORDER BY series_id, cutoff_id, forecast_step;

-- ============================================================================
-- STEP 7: Summary Statistics
-- ============================================================================
SELECT '=== STEP 7: Summary statistics ===' AS step;

WITH series_performance AS (
    SELECT 
        series_id,
        cutoff_id,
        AVG(abs_error) AS series_mae
    FROM rolling_evaluation
    GROUP BY series_id, cutoff_id
)
SELECT 
    'Distribution of MAE across series' AS metric,
    ROUND(MIN(series_mae), 3) AS min_mae,
    ROUND(PERCENTILE_CONT(0.25) WITHIN GROUP (ORDER BY series_mae), 3) AS p25_mae,
    ROUND(PERCENTILE_CONT(0.50) WITHIN GROUP (ORDER BY series_mae), 3) AS median_mae,
    ROUND(PERCENTILE_CONT(0.75) WITHIN GROUP (ORDER BY series_mae), 3) AS p75_mae,
    ROUND(MAX(series_mae), 3) AS max_mae
FROM series_performance;

.timer off

-- ============================================================================
-- KEY TAKEAWAYS
-- ============================================================================
-- 1. Rolling forecasts simulate real-world forecasting where you only have
--    past data available at prediction time
-- 2. Multiple cutoff points help assess model stability over time
-- 3. Forecast accuracy typically degrades with longer horizons
-- 4. Coverage (% of actuals within prediction intervals) should be ~95%
-- 5. Use rolling forecasts to compare different models fairly
-- ============================================================================

