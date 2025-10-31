-- Detect seasonality for each product category
WITH aggregated AS (
    SELECT 
        category,
        LIST(date ORDER BY date) AS timestamps,
        LIST(sales ORDER BY date) AS values
    FROM sales_data
    GROUP BY category
)
SELECT 
    category,
    TS_ANALYZE_SEASONALITY(timestamps, values) AS analysis
FROM aggregated;
