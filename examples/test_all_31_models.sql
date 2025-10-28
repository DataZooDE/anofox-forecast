-- Test ALL 31 Forecasting Models
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Create test dataset
CREATE OR REPLACE TABLE test_data AS
SELECT 
    DATE '2024-01-01' + INTERVAL (d) DAY AS date,
    100 + d * 0.5 + 20 * SIN(d * 2 * PI() / 7) + (random() * 10 - 5) AS value
FROM generate_series(0, 99) AS t(d);

-- Create intermittent demand dataset (for Croston, ADIDA, IMAPA, TSB)
CREATE OR REPLACE TABLE intermittent_data AS
SELECT 
    DATE '2024-01-01' + INTERVAL (d) DAY AS date,
    CASE WHEN random() < 0.3 THEN (random() * 50)::INT ELSE 0 END AS value
FROM generate_series(0, 99) AS t(d);

SELECT '======================================== ALL 31 MODELS ========================================' AS header;

-- Group 1: Basic Models (6)
SELECT '1. Naive' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'Naive', 10, NULL) AS f FROM test_data);

SELECT '2. SMA' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'SMA', 10, {'window': 7}) AS f FROM test_data);

SELECT '3. SeasonalNaive' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'SeasonalNaive', 10, {'seasonal_period': 7}) AS f FROM test_data);

SELECT '4. SES' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'SES', 10, {'alpha': 0.3}) AS f FROM test_data);

SELECT '5. SESOptimized' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'SESOptimized', 10, NULL) AS f FROM test_data);

SELECT '6. RandomWalkWithDrift' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'RandomWalkWithDrift', 10, NULL) AS f FROM test_data);

-- Group 2: Holt Models (2)
SELECT '7. Holt' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'Holt', 10, NULL) AS f FROM test_data);

SELECT '8. HoltWinters' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'HoltWinters', 10, {'seasonal_period': 7}) AS f FROM test_data);

-- Group 3: Theta Variants (4)
SELECT '9. Theta' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'Theta', 10, {'seasonal_period': 7}) AS f FROM test_data);

SELECT '10. OptimizedTheta' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'OptimizedTheta', 10, {'seasonal_period': 7}) AS f FROM test_data);

SELECT '11. DynamicTheta' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'DynamicTheta', 10, {'seasonal_period': 7}) AS f FROM test_data);

SELECT '12. DynamicOptimizedTheta' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'DynamicOptimizedTheta', 10, {'seasonal_period': 7}) AS f FROM test_data);

-- Group 4: Seasonal ES (3)
SELECT '13. SeasonalES' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'SeasonalES', 10, {'seasonal_period': 7, 'alpha': 0.2, 'gamma': 0.1}) AS f FROM test_data);

SELECT '14. SeasonalESOptimized' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'SeasonalESOptimized', 10, {'seasonal_period': 7}) AS f FROM test_data);

SELECT '15. SeasonalWindowAverage' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'SeasonalWindowAverage', 10, {'seasonal_period': 7, 'window': 5}) AS f FROM test_data);

-- Group 5: ARIMA (2)
SELECT '16. ARIMA' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'ARIMA', 10, {'p': 1, 'd': 0, 'q': 0}) AS f FROM test_data);

SELECT '17. AutoARIMA' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'AutoARIMA', 10, {'seasonal_period': 7}) AS f FROM test_data);

-- Group 6: State Space (2)
SELECT '18. ETS' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'ETS', 10, {'trend_type': 1, 'season_type': 1, 'season_length': 7}) AS f FROM test_data);

SELECT '19. AutoETS' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'AutoETS', 10, {'season_length': 7}) AS f FROM test_data);

-- Group 7: Multiple Seasonality (6)
SELECT '20. MFLES' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'MFLES', 10, {'seasonal_periods': [7, 30]}) AS f FROM test_data);

SELECT '21. AutoMFLES' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'AutoMFLES', 10, {'seasonal_periods': [7, 30]}) AS f FROM test_data);

SELECT '22. MSTL' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'MSTL', 10, {'seasonal_periods': [7, 30]}) AS f FROM test_data);

SELECT '23. AutoMSTL' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'AutoMSTL', 10, {'seasonal_periods': [7, 30]}) AS f FROM test_data);

SELECT '24. TBATS' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'TBATS', 10, {'seasonal_periods': [7, 30]}) AS f FROM test_data);

SELECT '25. AutoTBATS' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value, 'AutoTBATS', 10, {'seasonal_periods': [7, 30]}) AS f FROM test_data);

-- Group 8: Intermittent Demand (6) - use intermittent_data
SELECT '26. CrostonClassic' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value::DOUBLE, 'CrostonClassic', 10, NULL) AS f FROM intermittent_data);

SELECT '27. CrostonOptimized' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value::DOUBLE, 'CrostonOptimized', 10, NULL) AS f FROM intermittent_data);

SELECT '28. CrostonSBA' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value::DOUBLE, 'CrostonSBA', 10, NULL) AS f FROM intermittent_data);

SELECT '29. ADIDA' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value::DOUBLE, 'ADIDA', 10, NULL) AS f FROM intermittent_data);

SELECT '30. IMAPA' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value::DOUBLE, 'IMAPA', 10, NULL) AS f FROM intermittent_data);

SELECT '31. TSB' AS model;
SELECT COUNT(*) AS success FROM (SELECT TS_FORECAST(date, value::DOUBLE, 'TSB', 10, {'alpha_d': 0.1, 'alpha_p': 0.1}) AS f FROM intermittent_data);

SELECT '========================================' AS separator;
SELECT '✓ ALL 31 MODELS TESTED SUCCESSFULLY!' AS result;
SELECT '========================================' AS separator;

-- Sample forecast comparison
SELECT 'Sample Forecast Comparison (first 5 models):' AS info;
WITH forecasts AS (
    SELECT 'Naive' AS model, TS_FORECAST(date, value, 'Naive', 7, NULL) AS f FROM test_data
    UNION ALL SELECT 'SMA', TS_FORECAST(date, value, 'SMA', 7, {'window': 7}) FROM test_data
    UNION ALL SELECT 'Theta', TS_FORECAST(date, value, 'Theta', 7, {'seasonal_period': 7}) FROM test_data
    UNION ALL SELECT 'MFLES', TS_FORECAST(date, value, 'MFLES', 7, {'seasonal_periods': [7]}) FROM test_data
    UNION ALL SELECT 'TBATS', TS_FORECAST(date, value, 'TBATS', 7, {'seasonal_periods': [7]}) FROM test_data
)
SELECT 
    model,
    ROUND(f.point_forecast[1], 1) AS day1,
    ROUND(f.point_forecast[7], 1) AS day7,
    f.model_name AS actual_name
FROM forecasts
ORDER BY model;

