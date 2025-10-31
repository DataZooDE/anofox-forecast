-- Detect changepoints for each product
SELECT 
    group_col AS product_id,
    date_col AS date,
    is_changepoint
FROM TS_DETECT_CHANGEPOINTS_BY('sales_data', product_id, date, sales, MAP{})
WHERE is_changepoint = true
ORDER BY product_id, date;

-- Count changepoints per product
SELECT 
    group_col AS product_id,
    COUNT(*) FILTER (WHERE is_changepoint) AS num_changepoints
FROM TS_DETECT_CHANGEPOINTS_BY('sales_data', product_id, date, sales, MAP{})
GROUP BY product_id
ORDER BY num_changepoints DESC;
