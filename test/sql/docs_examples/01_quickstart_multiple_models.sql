-- Create sample data
CREATE TABLE my_sales AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Compare forecasts from different models using the same dataset
-- Model 1: AutoETS (automatic exponential smoothing)
SELECT 
    forecast_step, 
    date, 
    point_forecast,
    lower_90,
    upper_90
FROM TS_FORECAST('my_sales', date, sales, 'AutoETS', 14, MAP{'seasonal_period': 7})
LIMIT 5;

-- Model 2: SES (Simple Exponential Smoothing)
SELECT 
    forecast_step, 
    date, 
    point_forecast,
    lower_90,
    upper_90
FROM TS_FORECAST('my_sales', date, sales, 'SES', 14, MAP{})
LIMIT 5;

-- Model 3: Theta (theta decomposition method)
SELECT 
    forecast_step, 
    date, 
    point_forecast,
    lower_90,
    upper_90
FROM TS_FORECAST('my_sales', date, sales, 'Theta', 14, MAP{'seasonal_period': 7})
LIMIT 5;

