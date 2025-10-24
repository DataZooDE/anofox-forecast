-- ==============================================================================
-- Comprehensive Test Suite for DuckDB Time Series Extension
-- ==============================================================================

LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Create test dataset with multiple series
CREATE OR REPLACE TABLE test_series AS
SELECT 
    ('2024-01-01'::TIMESTAMP + INTERVAL (i) DAYS) AS date,
    'Series_' || (i % 3)::VARCHAR AS series_id,
    100.0 + i * 2.0 + (i % 7) * 5.0 AS value
FROM range(0, 60) t(i);

SELECT '=== TEST 1: Single Series Aggregation ===' AS test;
SELECT TS_FORECAST(date, value, 'Naive', 5, NULL) AS forecast
FROM test_series
WHERE series_id = 'Series_0';

SELECT '=== TEST 2: GROUP BY Multiple Series ===' AS test;
SELECT 
    series_id,
    TS_FORECAST(date, value, 'Naive', 5, NULL) AS forecast
FROM test_series
GROUP BY series_id
ORDER BY series_id;

SELECT '=== TEST 3: SMA with Default Parameters ===' AS test;
SELECT 
    series_id,
    TS_FORECAST(date, value, 'SMA', 5, NULL) AS forecast
FROM test_series
GROUP BY series_id
LIMIT 1;

SELECT '=== TEST 4: SMA with Custom Window ===' AS test;
SELECT 
    series_id,
    TS_FORECAST(date, value, 'SMA', 5, {'window': 10}) AS forecast
FROM test_series
GROUP BY series_id
LIMIT 1;

SELECT '=== TEST 5: SeasonalNaive with Parameters ===' AS test;
SELECT 
    series_id,
    TS_FORECAST(date, value, 'SeasonalNaive', 7, {'seasonal_period': 7}) AS forecast
FROM test_series
GROUP BY series_id
LIMIT 1;

SELECT '=== TEST 6: UNNEST for Row-wise Output ===' AS test;
SELECT 
    series_id,
    UNNEST(forecast.forecast_step) AS step,
    ROUND(UNNEST(forecast.point_forecast), 2) AS forecast_value
FROM (
    SELECT 
        series_id,
        TS_FORECAST(date, value, 'Naive', 5, NULL) AS forecast
    FROM test_series
    GROUP BY series_id
)
WHERE series_id = 'Series_0'
ORDER BY step;

SELECT '=== TEST 7: Parameter Comparison ===' AS test;
SELECT 
    config,
    ROUND(AVG(forecast), 2) AS avg_forecast
FROM (
    SELECT 
        'window=3' AS config,
        UNNEST(TS_FORECAST(date, value, 'SMA', 5, {'window': 3}).point_forecast) AS forecast
    FROM test_series
    UNION ALL
    SELECT 
        'window=7',
        UNNEST(TS_FORECAST(date, value, 'SMA', 5, {'window': 7}).point_forecast)
    FROM test_series
    UNION ALL
    SELECT 
        'window=14',
        UNNEST(TS_FORECAST(date, value, 'SMA', 5, {'window': 14}).point_forecast)
    FROM test_series
)
GROUP BY config
ORDER BY config;

SELECT '=== TEST 8: Confidence Intervals ===' AS test;
SELECT 
    series_id,
    ROUND(AVG(point_forecast), 2) AS avg_forecast,
    ROUND(AVG(lower_95), 2) AS avg_lower,
    ROUND(AVG(upper_95), 2) AS avg_upper,
    ROUND(AVG(upper_95 - lower_95), 2) AS avg_ci_width
FROM (
    SELECT 
        series_id,
        UNNEST(forecast.point_forecast) AS point_forecast,
        UNNEST(forecast.lower_95) AS lower_95,
        UNNEST(forecast.upper_95) AS upper_95
    FROM (
        SELECT 
            series_id,
            TS_FORECAST(date, value, 'Naive', 10, NULL) AS forecast
        FROM test_series
        GROUP BY series_id
    )
)
GROUP BY series_id
ORDER BY series_id;

SELECT '=== TEST 9: All Models Comparison ===' AS test;
SELECT 
    model,
    ROUND(AVG(forecast), 2) AS avg_forecast
FROM (
    SELECT 
        'Naive' AS model,
        UNNEST(TS_FORECAST(date, value, 'Naive', 5, NULL).point_forecast) AS forecast
    FROM test_series
    UNION ALL
    SELECT 
        'SMA',
        UNNEST(TS_FORECAST(date, value, 'SMA', 5, {'window': 7}).point_forecast)
    FROM test_series
    UNION ALL
    SELECT 
        'SeasonalNaive',
        UNNEST(TS_FORECAST(date, value, 'SeasonalNaive', 5, {'seasonal_period': 7}).point_forecast)
    FROM test_series
)
GROUP BY model
ORDER BY model;

SELECT '=== ALL TESTS COMPLETE ===' AS result;

DROP TABLE test_series;

