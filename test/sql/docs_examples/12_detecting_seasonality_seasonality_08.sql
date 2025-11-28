-- Create sample sales data
CREATE TABLE sales_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Detect seasonality in different time windows
WITH windows AS (
    SELECT 
        DATE_TRUNC('quarter', date) AS quarter,
        LIST(sales) AS values
    FROM sales_data
    GROUP BY quarter
)
SELECT 
    quarter,
    TS_DETECT_SEASONALITY(values) AS detected_periods
FROM windows
ORDER BY quarter;
