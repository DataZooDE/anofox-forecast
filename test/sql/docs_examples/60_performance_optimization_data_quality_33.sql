SELECT 
    COUNT(DISTINCT product_id) AS num_series,
    COUNT(*) AS total_rows,
    COUNT(*) / COUNT(DISTINCT product_id) AS avg_series_length
FROM sales;
