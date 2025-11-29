-- Create sample data
CREATE TABLE my_sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Forecast next 14 days
SELECT * FROM anofox_fcst_ts_forecast(
    'my_sales',      -- table name
    date,            -- date column
    sales,           -- value column
    'AutoETS',       -- model (automatic)
    14,              -- horizon (14 days)
    MAP{'seasonal_period': 7}  -- weekly seasonality
);
