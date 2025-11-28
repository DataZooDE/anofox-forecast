-- Create sample multi-category data
CREATE TABLE sales_data AS
SELECT 
    category,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('Electronics'), ('Clothing')) categories(category);

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
    TS_DETECT_SEASONALITY(values) AS detected_periods
FROM aggregated;
