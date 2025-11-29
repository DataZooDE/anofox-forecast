-- Create sample data
CREATE TABLE test_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 89) t(d);

-- These should work (ARIMA models are supported)
SELECT * FROM anofox_fcst_ts_forecast('test_data', date, value, 'ARIMA', 5, MAP{'p': 1, 'd': 1, 'q': 1})
LIMIT 5;

SELECT * FROM anofox_fcst_ts_forecast('test_data', date, value, 'AutoARIMA', 5, MAP{})
LIMIT 5;

-- Verify other models still work
SELECT * FROM anofox_fcst_ts_forecast('test_data', date, value, 'Naive', 5, MAP{})
LIMIT 5;
SELECT * FROM anofox_fcst_ts_forecast('test_data', date, value, 'AutoETS', 5, MAP{'seasonal_period': 12})
LIMIT 5;
