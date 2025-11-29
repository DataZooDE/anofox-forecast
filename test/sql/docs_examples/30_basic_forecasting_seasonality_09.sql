-- Create sample data
CREATE TABLE sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Try 3 models and compare
WITH ets AS (
    SELECT 'AutoETS' AS model, * 
    FROM anofox_fcst_ts_forecast('sales', date, sales, 'AutoETS', 14, MAP{'seasonal_period': 7})
),
theta AS (
    SELECT 'Theta' AS model, * 
    FROM anofox_fcst_ts_forecast('sales', date, sales, 'Theta', 14, MAP{'seasonal_period': 7})
),
naive AS (
    SELECT 'Theta' AS model, * 
    FROM anofox_fcst_ts_forecast('sales', date, sales, 'Theta', 14, MAP{'seasonal_period': 7})
)
SELECT * FROM ets 
UNION ALL SELECT * FROM theta 
UNION ALL SELECT * FROM naive
ORDER BY model, forecast_step;
