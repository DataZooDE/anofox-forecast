-- Create sample multi-product data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + (ROW_NUMBER() OVER (PARTITION BY product_id ORDER BY product_id) % 3 + 1) * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

CREATE TABLE stats AS
SELECT * FROM anofox_fcst_ts_stats('sales', product_id, date, sales_amount, '1d');

-- View summary
SELECT 
    COUNT(*) AS total_series,
    ROUND(AVG(CAST(length AS DOUBLE)), 4) AS avg_length,
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END) AS series_with_nulls
FROM stats;
