-- Create sample data
CREATE TABLE test_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 89) t(d);

-- Negative horizon - should error
-- SELECT * FROM anofox_fcst_ts_forecast('test_data', date, value, 'Naive', -5, MAP{});
-- Expected: Error

-- Invalid confidence level - should error
-- SELECT * FROM anofox_fcst_ts_forecast('test_data', date, value, 'Naive', 5, MAP{'confidence_level': 1.5});
-- Expected: Error

-- Invalid model name - should error
-- SELECT * FROM anofox_fcst_ts_forecast('test_data', date, value, 'NonExistentModel', 5, MAP{});
-- Expected: Clear error message

-- Valid usage for comparison
SELECT * FROM anofox_fcst_ts_forecast('test_data', date, value, 'Naive', 5, MAP{})
LIMIT 5;
