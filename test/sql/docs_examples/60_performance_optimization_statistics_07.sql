-- Compute once
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales', product_id, date, amount);

-- Reuse many times
SELECT * FROM sales_stats WHERE quality_score < 0.7;
SELECT * FROM TS_DATASET_SUMMARY('sales_stats');
SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30);
