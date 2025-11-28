-- Create sample sales data
CREATE TABLE sales_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Forecast using only data after the last changepoint
WITH last_cp AS (
    SELECT MAX(date_col) AS last_change
    FROM TS_DETECT_CHANGEPOINTS('sales_data', date, sales, MAP{})
    WHERE is_changepoint = true
)
SELECT * FROM TS_FORECAST(
    'sales_data',
    date, sales, 'AutoETS', 28, MAP{'seasonal_period': 7}
)
WHERE date > (SELECT last_change FROM last_cp);
