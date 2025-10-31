-- Detect changepoints across 1000s of products
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales,
    is_changepoint
FROM TS_DETECT_CHANGEPOINTS_BY('product_sales', product_id, date, sales, MAP{})
WHERE is_changepoint = true
ORDER BY product_id, date;

-- Find products with recent changes
WITH changes AS (
    SELECT 
        group_col AS product_id,
        MAX(date_col) FILTER (WHERE is_changepoint) AS last_change
    FROM TS_DETECT_CHANGEPOINTS_BY('product_sales', product_id, date, sales, MAP{})
    GROUP BY product_id
)
SELECT 
    product_id,
    last_change,
    DATE_DIFF('day', last_change, CURRENT_DATE) AS days_since_change
FROM changes
WHERE last_change >= CURRENT_DATE - INTERVAL '30 days'
ORDER BY last_change DESC;
