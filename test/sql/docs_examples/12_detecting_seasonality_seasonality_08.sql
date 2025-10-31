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
