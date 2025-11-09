-- Test AutoTheta on classic AirPassengers dataset pattern
-- Monthly airline passenger numbers (trending + seasonal)

CREATE TABLE airpassengers AS 
VALUES
    (DATE '1949-01-01', 112), (DATE '1949-02-01', 118), (DATE '1949-03-01', 132),
    (DATE '1949-04-01', 129), (DATE '1949-05-01', 121), (DATE '1949-06-01', 135),
    (DATE '1949-07-01', 148), (DATE '1949-08-01', 148), (DATE '1949-09-01', 136),
    (DATE '1949-10-01', 119), (DATE '1949-11-01', 104), (DATE '1949-12-01', 118),
    (DATE '1950-01-01', 115), (DATE '1950-02-01', 126), (DATE '1950-03-01', 141),
    (DATE '1950-04-01', 135), (DATE '1950-05-01', 125), (DATE '1950-06-01', 149),
    (DATE '1950-07-01', 170), (DATE '1950-08-01', 170), (DATE '1950-09-01', 158),
    (DATE '1950-10-01', 133), (DATE '1950-11-01', 114), (DATE '1950-12-01', 140),
    (DATE '1951-01-01', 145), (DATE '1951-02-01', 150), (DATE '1951-03-01', 178),
    (DATE '1951-04-01', 163), (DATE '1951-05-01', 172), (DATE '1951-06-01', 178),
    (DATE '1951-07-01', 199), (DATE '1951-08-01', 199), (DATE '1951-09-01', 184),
    (DATE '1951-10-01', 162), (DATE '1951-11-01', 146), (DATE '1951-12-01', 166)
AS t(date, passengers);

-- Test 1: AutoTheta forecast on AirPassengers
SELECT 
    'AutoTheta AirPassengers' as test,
    COUNT(*) as forecast_length,
    ROUND(MIN(forecast), 2) as min_forecast,
    ROUND(MAX(forecast), 2) as max_forecast,
    ROUND(AVG(forecast), 2) as avg_forecast
FROM TS_FORECAST('airpassengers', date, passengers, 'AutoTheta', 12, 
    {'seasonal_period': 12});

-- Test 2: Compare with manual Theta variants
WITH comparison AS (
    SELECT 'AutoTheta' as method, ROUND(AVG(forecast), 2) as avg_12month_forecast
    FROM TS_FORECAST('airpassengers', date, passengers, 'AutoTheta', 12, {'seasonal_period': 12})
    
    UNION ALL
    
    SELECT 'Theta (STM)' as method, ROUND(AVG(forecast), 2) as avg_12month_forecast
    FROM TS_FORECAST('airpassengers', date, passengers, 'Theta', 12, {'seasonal_period': 12, 'theta': 2.0})
    
    UNION ALL
    
    SELECT 'OptimizedTheta (OTM)' as method, ROUND(AVG(forecast), 2) as avg_12month_forecast
    FROM TS_FORECAST('airpassengers', date, passengers, 'OptimizedTheta', 12, {'seasonal_period': 12})
)
SELECT * FROM comparison ORDER BY method;

-- Test 3: Verify forecast increases (since AirPassengers is trending up)
WITH forecast_data AS (
    SELECT 
        ROW_NUMBER() OVER (ORDER BY (SELECT NULL)) as step,
        forecast
    FROM TS_FORECAST('airpassengers', date, passengers, 'AutoTheta', 12, {'seasonal_period': 12})
)
SELECT 
    'Trend check' as test,
    MAX(forecast) > MIN(forecast) as has_trend,
    AVG(CASE WHEN step <= 6 THEN forecast END) < AVG(CASE WHEN step > 6 THEN forecast END) as upward_trend
FROM forecast_data;

-- Cleanup
DROP TABLE airpassengers;

