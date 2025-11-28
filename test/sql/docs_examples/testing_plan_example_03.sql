-- Create sample test data
CREATE TABLE test_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 30) t(d);

-- Test default confidence level (0.90) - check lower/upper bounds
SELECT 
    COUNT(*) AS forecast_count,
    MIN(lower_90) AS min_lower,
    MAX(upper_90) AS max_upper
FROM TS_FORECAST('test_data', date, value, 'Naive', 5, MAP{});
-- Expected: 5 forecasts with 90% confidence intervals

-- Test custom confidence level
SELECT 
    COUNT(*) AS forecast_count,
    MIN(lower_95) AS min_lower,
    MAX(upper_95) AS max_upper
FROM TS_FORECAST('test_data', date, value, 'Naive', 5, MAP{'confidence_level': 0.95});
-- Expected: 5 forecasts with 95% confidence intervals
