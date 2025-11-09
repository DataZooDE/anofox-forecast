-- Test AutoTheta with seasonal data
-- Verify automatic seasonality detection and decomposition

-- Create monthly seasonal data (3 years)
CREATE TABLE monthly_data AS 
SELECT 
    DATE '2020-01-01' + INTERVAL (i) MONTH AS date,
    100.0 + i * 0.5 + SIN(2 * PI() * i / 12) * 20.0 AS value
FROM range(0, 36) t(i);

-- Test 1: AutoTheta with monthly seasonality (auto decomposition)
SELECT 
    'AutoTheta monthly auto' as test,
    COUNT(*) as forecast_length,
    MIN(forecast) as min_forecast,
    MAX(forecast) as max_forecast
FROM TS_FORECAST('monthly_data', date, value, 'AutoTheta', 12, 
    {'seasonal_period': 12});

-- Test 2: Compare AutoTheta with specific Theta variants
WITH forecasts AS (
    SELECT 'AutoTheta' as model, AVG(forecast) as avg_forecast
    FROM TS_FORECAST('monthly_data', date, value, 'AutoTheta', 12, {'seasonal_period': 12})
    UNION ALL
    SELECT 'Theta' as model, AVG(forecast) as avg_forecast
    FROM TS_FORECAST('monthly_data', date, value, 'Theta', 12, {'seasonal_period': 12, 'theta': 2.0})
    UNION ALL
    SELECT 'OptimizedTheta' as model, AVG(forecast) as avg_forecast
    FROM TS_FORECAST('monthly_data', date, value, 'OptimizedTheta', 12, {'seasonal_period': 12})
)
SELECT * FROM forecasts ORDER BY model;

-- Test 3: Non-seasonal data (period = 1)
CREATE TABLE non_seasonal_data AS 
SELECT 
    DATE '2020-01-01' + INTERVAL (i) DAY AS date,
    100.0 + i * 0.3 AS value
FROM range(0, 100) t(i);

SELECT 
    'AutoTheta non-seasonal' as test,
    COUNT(*) as forecast_length
FROM TS_FORECAST('non_seasonal_data', date, value, 'AutoTheta', 10, 
    {'seasonal_period': 1});

-- Cleanup
DROP TABLE monthly_data;
DROP TABLE non_seasonal_data;

