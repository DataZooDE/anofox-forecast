-- Generate statistics
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_raw', product_id, date, amount);

-- View summary
SELECT * FROM TS_DATASET_SUMMARY('sales_stats');

-- Quality report
SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30);
