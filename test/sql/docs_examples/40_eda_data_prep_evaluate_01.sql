-- Compute comprehensive stats for all series
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_raw', product_id, date, sales_amount);

-- View results
SELECT * FROM sales_stats LIMIT 5;
