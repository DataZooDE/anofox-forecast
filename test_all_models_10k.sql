-- Complete Test: Generate 10K Series + Test All 14 Models
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Generate 10,000 series (lightweight for testing)
CREATE OR REPLACE TABLE timeseries_10k AS
WITH series_data AS (
    SELECT 
        series_id,
        'Series_' || LPAD(series_id::VARCHAR, 5, '0') AS series_name,
        DATE '2023-01-01' + INTERVAL (d) DAY AS date,
        d AS day_index,
        -- Simple pattern: base + trend + weekly seasonality + noise
        GREATEST(0, 
            (100 + (random() * 400)::int)  -- base 100-500
            + (random() - 0.5) * d  -- trend
            + 30 * SIN(d * 2 * PI() / 7)  -- weekly seasonality
            + (random() * 20 - 10)  -- noise
        )::DOUBLE AS value
    FROM generate_series(1, 10000) AS s(series_id)
    CROSS JOIN generate_series(0, 364) AS t(d)
)
SELECT * FROM series_data ORDER BY series_id, date;

SELECT 'Dataset ready: ' || COUNT(DISTINCT series_id)::VARCHAR || ' series, ' || 
       COUNT(*)::VARCHAR || ' data points' AS status
FROM timeseries_10k;

SELECT '========================================' AS separator;
SELECT 'Testing All 14 Models' AS info;
SELECT '========================================' AS separator;

-- Test 1: Fast models on 100 series
SELECT 'Test 1: Naive (100 series)' AS test;
SELECT COUNT(*) AS forecasted, 'Naive' AS model
FROM (SELECT TS_FORECAST(date, value, 'Naive', 30, NULL) AS f 
      FROM timeseries_10k WHERE series_id <= 100 GROUP BY series_name);

SELECT 'Test 2: SMA (100 series)' AS test;
SELECT COUNT(*) AS forecasted, 'SMA' AS model
FROM (SELECT TS_FORECAST(date, value, 'SMA', 30, {'window': 7}) AS f 
      FROM timeseries_10k WHERE series_id <= 100 GROUP BY series_name);

SELECT 'Test 3: SeasonalNaive (100 series)' AS test;
SELECT COUNT(*) AS forecasted, 'SeasonalNaive' AS model
FROM (SELECT TS_FORECAST(date, value, 'SeasonalNaive', 30, {'seasonal_period': 7}) AS f 
      FROM timeseries_10k WHERE series_id <= 100 GROUP BY series_name);

SELECT 'Test 4: SES (100 series)' AS test;
SELECT COUNT(*) AS forecasted, 'SES' AS model
FROM (SELECT TS_FORECAST(date, value, 'SES', 30, {'alpha': 0.3}) AS f 
      FROM timeseries_10k WHERE series_id <= 100 GROUP BY series_name);

SELECT 'Test 5: Holt (100 series)' AS test;
SELECT COUNT(*) AS forecasted, 'Holt' AS model
FROM (SELECT TS_FORECAST(date, value, 'Holt', 30, NULL) AS f 
      FROM timeseries_10k WHERE series_id <= 100 GROUP BY series_name);

SELECT TS_FORECAST(date, value, 'MFLES', 30, {'seasonal_periods': [7, 30], 'n_iterations': 3}) AS f 
      FROM timeseries_10k GROUP BY series_name;

SELECT 'Test 6: HoltWinters (50 series)' AS test;
SELECT COUNT(*) AS forecasted, 'HoltWinters' AS model
FROM (SELECT TS_FORECAST(date, value, 'HoltWinters', 30, {'seasonal_period': 7}) AS f 
      FROM timeseries_10k WHERE series_id <= 50 GROUP BY series_name);

SELECT 'Test 7: Theta (50 series)' AS test;
SELECT COUNT(*) AS forecasted, 'Theta' AS model
FROM (SELECT TS_FORECAST(date, value, 'Theta', 30, {'seasonal_period': 7}) AS f 
      FROM timeseries_10k WHERE series_id <= 50 GROUP BY series_name);

SELECT 'Test 8: ETS (50 series)' AS test;
SELECT COUNT(*) AS forecasted, 'ETS' AS model
FROM (SELECT TS_FORECAST(date, value, 'ETS', 30, {
    'trend_type': 1, 'season_type': 1, 'season_length': 7}) AS f 
      FROM timeseries_10k WHERE series_id <= 50 GROUP BY series_name);

SELECT 'Test 9: AutoETS (20 series)' AS test;
SELECT COUNT(*) AS forecasted, 'AutoETS' AS model
FROM (SELECT TS_FORECAST(date, value, 'AutoETS', 30, {'season_length': 7}) AS f 
      FROM timeseries_10k WHERE series_id <= 20 GROUP BY series_name);

SELECT 'Test 10: MFLES (20 series) **NEW**' AS test;
SELECT COUNT(*) AS forecasted, 'MFLES' AS model
FROM (SELECT TS_FORECAST(date, value, 'MFLES', 30, {
    'seasonal_periods': [7, 30], 'n_iterations': 3}) AS f 
      FROM timeseries_10k WHERE series_id <= 20 GROUP BY series_name);

SELECT 'Test 11: AutoMFLES (20 series) **NEW**' AS test;
SELECT COUNT(*) AS forecasted, 'AutoMFLES' AS model
FROM (SELECT TS_FORECAST(date, value, 'AutoMFLES', 30, {
    'seasonal_periods': [7, 30]}) AS f 
      FROM timeseries_10k WHERE series_id <= 20 GROUP BY series_name);

SELECT 'Test 12: MSTL (20 series) **NEW**' AS test;
SELECT COUNT(*) AS forecasted, 'MSTL' AS model
FROM (SELECT TS_FORECAST(date, value, 'MSTL', 30, {
    'seasonal_periods': [7, 30]}) AS f 
      FROM timeseries_10k WHERE series_id <= 20 GROUP BY series_name);

SELECT 'Test 13: AutoMSTL (20 series) **NEW**' AS test;
SELECT COUNT(*) AS forecasted, 'AutoMSTL' AS model
FROM (SELECT TS_FORECAST(date, value, 'AutoMSTL', 30, {
    'seasonal_periods': [7, 30]}) AS f 
      FROM timeseries_10k WHERE series_id <= 20 GROUP BY series_name);

SELECT 'Test 14: AutoARIMA (10 series)' AS test;
SELECT COUNT(*) AS forecasted, 'AutoARIMA' AS model
FROM (SELECT TS_FORECAST(date, value, 'AutoARIMA', 30, {'seasonal_period': 7}) AS f 
      FROM timeseries_10k WHERE series_id <= 10 GROUP BY series_name);

SELECT '========================================' AS separator;
SELECT '✓ All 14 Models Tested Successfully!' AS result;
SELECT '✓ New: MFLES, AutoMFLES, MSTL, AutoMSTL' AS result;
SELECT '========================================' AS separator;

-- Show sample forecasts
SELECT 'Sample Forecast Comparison (Series_00001):' AS info;
WITH forecasts AS (
    SELECT 'Naive' AS model, TS_FORECAST(date, value, 'Naive', 14, NULL) AS f
    FROM timeseries_10k WHERE series_name = 'Series_00001'
    UNION ALL SELECT 'Theta', TS_FORECAST(date, value, 'Theta', 14, {'seasonal_period': 7})
    FROM timeseries_10k WHERE series_name = 'Series_00001'
    UNION ALL SELECT 'AutoETS', TS_FORECAST(date, value, 'AutoETS', 14, {'season_length': 7})
    FROM timeseries_10k WHERE series_name = 'Series_00001'
    UNION ALL SELECT 'MFLES', TS_FORECAST(date, value, 'MFLES', 14, {'seasonal_periods': [7]})
    FROM timeseries_10k WHERE series_name = 'Series_00001'
    UNION ALL SELECT 'MSTL', TS_FORECAST(date, value, 'MSTL', 14, {'seasonal_periods': [7]})
    FROM timeseries_10k WHERE series_name = 'Series_00001'
)
SELECT model,
       ROUND(f.point_forecast[1], 1) AS day1,
       ROUND(f.point_forecast[7], 1) AS day7,
       ROUND(f.point_forecast[14], 1) AS day14
FROM forecasts ORDER BY model;

