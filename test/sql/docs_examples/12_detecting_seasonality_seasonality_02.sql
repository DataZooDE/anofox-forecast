-- Create sample sales data
CREATE TABLE sales_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + 20 * SIN(2 * PI() * d / 30) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

WITH aggregated AS (
    SELECT LIST(sales ORDER BY date) AS values
    FROM sales_data
)
SELECT anofox_fcst_ts_detect_seasonality(values) AS periods
FROM aggregated;

-- Result: [7, 30]  (weekly and monthly seasonality)
