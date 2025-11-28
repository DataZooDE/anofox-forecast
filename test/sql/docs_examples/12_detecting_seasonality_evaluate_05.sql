-- Create sample sales data
CREATE TABLE sales_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Detect seasonality and use it for forecasting
WITH detection AS (
    SELECT TS_DETECT_SEASONALITY(LIST(sales ORDER BY date)) AS periods
    FROM sales_data
)
SELECT * FROM TS_FORECAST(
    'sales_data',
    date,
    sales,
    'AutoETS',  -- Use AutoETS which handles seasonality automatically
    28,
    MAP{'seasonal_period': 7}  -- Use detected or known period
);
