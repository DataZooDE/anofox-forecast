-- Create sample sales data with gaps
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20) AS sales_amount
FROM generate_series(0, 364) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id)
WHERE d % 3 != 0;  -- Create gaps by skipping some days

-- Generate statistics
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_raw', product_id, date, sales_amount);

-- Detect gaps
SELECT series_id, n_gaps, quality_score
FROM sales_stats
WHERE n_gaps > 0
ORDER BY n_gaps DESC
LIMIT 10;

-- Fix: Fill gaps
CREATE TABLE fixed AS
SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount, '1d');
