-- Create sample sales data
CREATE TABLE sales_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + 20 * SIN(2 * PI() * d / 30) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d);  -- 90 days of data

WITH aggregated AS (
    SELECT 
        LIST(date ORDER BY date) AS timestamps,
        LIST(sales ORDER BY date) AS values
    FROM sales_data
)
SELECT 
    result.detected_periods AS periods,
    result.primary_period AS primary,
    ROUND(result.seasonal_strength, 3) AS seasonal_str,
    ROUND(result.trend_strength, 3) AS trend_str
FROM (
    SELECT TS_ANALYZE_SEASONALITY(timestamps, values) AS result
    FROM aggregated
);

-- Result:
-- periods: [7, 30, 91]
-- primary: 7
-- seasonal_str: 0.842
-- trend_str: 0.156
