-- Create sample multi-product data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Check data quality before forecasting
CREATE TABLE stats AS SELECT * FROM anofox_fcst_ts_stats('sales', product_id, date, sales_amount, '1d');

-- Look for problems
SELECT 
    series_id,
    length,
    n_null,
    is_constant
FROM stats
WHERE n_null > 0 OR is_constant = true;
