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
SELECT * FROM anofox_fcst_ts_stats('sales_raw', product_id, date, sales_amount, '1d');

-- Detect gaps (check expected_length vs length)
SELECT 
    series_id, 
    expected_length - length AS n_gaps,
    length,
    expected_length
FROM sales_stats
WHERE expected_length > length
ORDER BY (expected_length - length) DESC
LIMIT 10;

-- Fix: Fill gaps
CREATE TABLE fixed AS
SELECT * FROM anofox_fcst_ts_fill_gaps('sales_raw', product_id, date, sales_amount, '1d');
