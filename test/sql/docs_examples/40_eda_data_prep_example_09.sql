-- Detect
SELECT series_id, n_gaps, quality_score
FROM sales_stats
WHERE n_gaps > 0
ORDER BY n_gaps DESC
LIMIT 10;

-- Fix
CREATE TABLE fixed AS
SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount);
