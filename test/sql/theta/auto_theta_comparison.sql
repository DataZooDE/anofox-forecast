-- Compare AutoTheta against individual Theta variants
-- Verify that AutoTheta selects reasonable models

-- Create test dataset with different patterns
CREATE TABLE comparison_data AS 
SELECT 
    DATE '2020-01-01' + INTERVAL (i) DAY AS date,
    100.0 + i * 0.2 + SIN(2 * PI() * i / 7) * 15.0 + (RANDOM() - 0.5) * 5.0 AS value
FROM range(0, 70) t(i);

-- Compare forecasts from all Theta variants
WITH all_forecasts AS (
    -- AutoTheta (automatic selection)
    SELECT 
        'AutoTheta' as model,
        AVG(forecast) as avg_forecast,
        STDDEV(forecast) as std_forecast
    FROM TS_FORECAST('comparison_data', date, value, 'AutoTheta', 7, {'seasonal_period': 7})
    
    UNION ALL
    
    -- Standard Theta Method
    SELECT 
        'STM (Theta)' as model,
        AVG(forecast) as avg_forecast,
        STDDEV(forecast) as std_forecast
    FROM TS_FORECAST('comparison_data', date, value, 'Theta', 7, {'seasonal_period': 7, 'theta': 2.0})
    
    UNION ALL
    
    -- Optimized Theta Method
    SELECT 
        'OTM (OptimizedTheta)' as model,
        AVG(forecast) as avg_forecast,
        STDDEV(forecast) as std_forecast
    FROM TS_FORECAST('comparison_data', date, value, 'OptimizedTheta', 7, {'seasonal_period': 7})
    
    UNION ALL
    
    -- Dynamic Optimized Theta Method
    SELECT 
        'DOTM (DynamicOptimizedTheta)' as model,
        AVG(forecast) as avg_forecast,
        STDDEV(forecast) as std_forecast
    FROM TS_FORECAST('comparison_data', date, value, 'DynamicOptimizedTheta', 7, {'seasonal_period': 7})
)
SELECT 
    model,
    ROUND(avg_forecast, 2) as avg_forecast,
    ROUND(std_forecast, 2) as std_forecast
FROM all_forecasts
ORDER BY model;

-- Test that AutoTheta produces valid forecasts
WITH auto_forecast AS (
    SELECT forecast 
    FROM TS_FORECAST('comparison_data', date, value, 'AutoTheta', 7, {'seasonal_period': 7})
)
SELECT 
    'Validation checks' as test,
    COUNT(*) = 7 as correct_length,
    MIN(forecast) > 0 as all_positive,
    MAX(forecast) < 1000 as reasonable_range,
    COUNT(CASE WHEN forecast IS NULL THEN 1 END) = 0 as no_nulls
FROM auto_forecast;

-- Cleanup
DROP TABLE comparison_data;

