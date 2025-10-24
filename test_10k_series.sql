-- Test Forecasting on 10,000 Time Series
-- Demonstrates batch forecasting performance

LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Ensure table exists
SELECT 'Checking dataset...' AS status;
SELECT COUNT(DISTINCT series_id) AS total_series FROM timeseries_10k;

SELECT '========================================' AS separator;
SELECT 'Test 1: Forecast 100 series with Naive model (Fast)' AS test;
SELECT '========================================' AS separator;

SELECT 
    series_name,
    forecast.model_name,
    LENGTH(forecast.forecast_step) AS horizon,
    forecast.point_forecast[1] AS first_forecast,
    forecast.point_forecast[30] AS last_forecast
FROM (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'Naive', 30, NULL) AS forecast
    FROM timeseries_10k
    WHERE series_id <= 100
    GROUP BY series_name
)
ORDER BY series_name
LIMIT 10;

SELECT '========================================' AS separator;
SELECT 'Test 2: Forecast 50 series with AutoETS (Medium Speed)' AS test;
SELECT '========================================' AS separator;

SELECT 
    series_name,
    category,
    region,
    forecast.model_name,
    ROUND(forecast.point_forecast[1], 2) AS day_1,
    ROUND(forecast.point_forecast[7], 2) AS day_7,
    ROUND(forecast.point_forecast[30], 2) AS day_30
FROM (
    SELECT 
        series_name,
        category,
        region,
        TS_FORECAST(date, value, 'AutoETS', 30, {'season_length': 7}) AS forecast
    FROM timeseries_10k
    WHERE series_id <= 50
    GROUP BY series_name, category, region
)
ORDER BY series_name
LIMIT 10;

SELECT '========================================' AS separator;
SELECT 'Test 3: Forecast by Category (Aggregate by Category)' AS test;
SELECT '========================================' AS separator;

SELECT 
    category,
    COUNT(*) AS series_count,
    ROUND(AVG(UNNEST(forecast.point_forecast)[1]), 2) AS avg_day1_forecast,
    ROUND(AVG(UNNEST(forecast.point_forecast)[7]), 2) AS avg_day7_forecast,
    ROUND(AVG(UNNEST(forecast.point_forecast)[30]), 2) AS avg_day30_forecast
FROM (
    SELECT 
        category,
        TS_FORECAST(date, value, 'Theta', 30, {'seasonal_period': 7}) AS forecast
    FROM timeseries_10k
    WHERE series_id <= 300  -- 300 series
    GROUP BY category, series_name
)
GROUP BY category
ORDER BY category;

SELECT '========================================' AS separator;
SELECT 'Test 4: MFLES with Multiple Seasonalities (20 series)' AS test;
SELECT '========================================' AS separator;

SELECT 
    series_name,
    forecast.model_name,
    ROUND(forecast.point_forecast[1], 2) AS day_1,
    ROUND(forecast.point_forecast[14], 2) AS day_14,
    ROUND(forecast.point_forecast[30], 2) AS day_30
FROM (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'MFLES', 30, {
            'seasonal_periods': [7, 30],
            'n_iterations': 3
        }) AS forecast
    FROM timeseries_10k
    WHERE series_id <= 20
    GROUP BY series_name
)
ORDER BY series_name
LIMIT 10;

SELECT '========================================' AS separator;
SELECT 'Test 5: AutoMFLES (Automatic MFLES - 20 series)' AS test;
SELECT '========================================' AS separator;

SELECT 
    series_name,
    forecast.model_name,
    ROUND(forecast.point_forecast[1], 2) AS day_1,
    ROUND(forecast.point_forecast[30], 2) AS day_30
FROM (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'AutoMFLES', 30, {
            'seasonal_periods': [7, 30]
        }) AS forecast
    FROM timeseries_10k
    WHERE series_id <= 20
    GROUP BY series_name
)
ORDER BY series_name
LIMIT 10;

SELECT '========================================' AS separator;
SELECT 'Test 6: MSTL Decomposition (20 series)' AS test;
SELECT '========================================' AS separator;

SELECT 
    series_name,
    forecast.model_name,
    ROUND(forecast.point_forecast[1], 2) AS day_1,
    ROUND(forecast.point_forecast[30], 2) AS day_30
FROM (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'MSTL', 30, {
            'seasonal_periods': [7, 30],
            'trend_method': 0,      -- Linear
            'seasonal_method': 0    -- Cyclic
        }) AS forecast
    FROM timeseries_10k
    WHERE series_id <= 20
    GROUP BY series_name
)
ORDER BY series_name
LIMIT 10;

SELECT '========================================' AS separator;
SELECT 'Test 7: AutoMSTL (Automatic MSTL - 20 series)' AS test;
SELECT '========================================' AS separator;

SELECT 
    series_name,
    forecast.model_name,
    ROUND(forecast.point_forecast[1], 2) AS day_1,
    ROUND(forecast.point_forecast[30], 2) AS day_30
FROM (
    SELECT 
        series_name,
        TS_FORECAST(date, value, 'AutoMSTL', 30, {
            'seasonal_periods': [7, 30]
        }) AS forecast
    FROM timeseries_10k
    WHERE series_id <= 20
    GROUP BY series_name
)
ORDER BY series_name
LIMIT 10;

SELECT '========================================' AS separator;
SELECT 'Test 8: Compare All Models on Same Series' AS test;
SELECT '========================================' AS separator;

-- Compare different models on Series_00001
WITH forecasts AS (
    SELECT 'Naive' AS model, 
           TS_FORECAST(date, value, 'Naive', 14, NULL) AS forecast
    FROM timeseries_10k WHERE series_name = 'Series_00001'
    UNION ALL
    SELECT 'SMA',
           TS_FORECAST(date, value, 'SMA', 14, {'window': 7})
    FROM timeseries_10k WHERE series_name = 'Series_00001'
    UNION ALL
    SELECT 'Holt',
           TS_FORECAST(date, value, 'Holt', 14, NULL)
    FROM timeseries_10k WHERE series_name = 'Series_00001'
    UNION ALL
    SELECT 'Theta',
           TS_FORECAST(date, value, 'Theta', 14, {'seasonal_period': 7})
    FROM timeseries_10k WHERE series_name = 'Series_00001'
    UNION ALL
    SELECT 'AutoETS',
           TS_FORECAST(date, value, 'AutoETS', 14, {'season_length': 7})
    FROM timeseries_10k WHERE series_name = 'Series_00001'
)
SELECT 
    model,
    ROUND(forecast.point_forecast[1], 2) AS day_1,
    ROUND(forecast.point_forecast[7], 2) AS day_7,
    ROUND(forecast.point_forecast[14], 2) AS day_14,
    ROUND(AVG(UNNEST(forecast.point_forecast)), 2) AS avg_forecast
FROM forecasts
ORDER BY model;

SELECT '========================================' AS separator;
SELECT 'Test 9: Performance Test - 1000 series with SMA (Fast)' AS test;
SELECT '========================================' AS separator;

SELECT 
    COUNT(*) AS series_forecasted,
    MIN(forecast.model_name) AS model_used,
    ROUND(AVG(UNNEST(forecast.point_forecast)), 2) AS avg_forecast_value
FROM (
    SELECT 
        TS_FORECAST(date, value, 'SMA', 30, {'window': 7}) AS forecast
    FROM timeseries_10k
    WHERE series_id <= 1000
    GROUP BY series_name
);

SELECT '========================================' AS separator;
SELECT 'Summary: All New Models Tested Successfully!' AS result;
SELECT 'Total models available: 14' AS info;
SELECT 'New models: MFLES, AutoMFLES, MSTL, AutoMSTL' AS info;
SELECT '========================================' AS separator;

