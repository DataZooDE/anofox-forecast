-- Create sample data
CREATE TABLE my_sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Simple ASCII visualization
WITH fc AS (
    SELECT 
        forecast_step,
        point_forecast,
        REPEAT('█', CAST(point_forecast / 5 AS INT)) AS bar
    FROM anofox_fcst_ts_forecast('my_sales', date, sales, 'AutoETS', 14, MAP{'seasonal_period': 7})
)
SELECT forecast_step, ROUND(point_forecast, 1) AS forecast, bar
FROM fc;
