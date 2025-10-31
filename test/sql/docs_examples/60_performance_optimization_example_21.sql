-- Cache preparation results
CREATE TABLE sales_prepared AS
SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, amount);

-- Materialized view for stats (refresh daily)
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_prepared', product_id, date, amount);

-- Use throughout the day without recomputation
SELECT * FROM sales_stats WHERE quality_score < 0.7;
