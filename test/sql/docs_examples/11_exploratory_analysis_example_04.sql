-- Create sample multi-product data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

CREATE TABLE stats AS
SELECT * FROM TS_STATS('sales', product_id, date, sales_amount, '1d');

-- Find series with low quality (short length or constant)
SELECT 
    series_id,
    length,
    is_constant,
    n_null,
    expected_length - length AS gaps
FROM stats
WHERE length < 60 OR is_constant = true;
