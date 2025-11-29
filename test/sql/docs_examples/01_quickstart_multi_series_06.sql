-- Create sample data
CREATE TABLE my_sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Compare models
-- Model 1: AutoETS (doesn't require seasonal_period, will auto-detect)
SELECT * FROM anofox_fcst_ts_forecast('my_sales', date, sales, 'AutoETS', 14, MAP{'seasonal_period': 7}) LIMIT 5;

-- Model 2: Theta
SELECT * FROM anofox_fcst_ts_forecast('my_sales', date, sales, 'Theta', 14, MAP{'seasonal_period': 7}) LIMIT 5;

-- Model 3: AutoARIMA
SELECT * FROM anofox_fcst_ts_forecast('my_sales', date, sales, 'AutoARIMA', 14, MAP{'seasonal_period': 7}) LIMIT 5;
