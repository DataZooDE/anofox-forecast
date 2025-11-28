-- Create sample multi-product data
CREATE TABLE my_sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Analyze your data before forecasting
CREATE TABLE stats AS
SELECT * FROM TS_STATS('my_sales', product_id, date, sales, '1d');

-- Generate quality report
SELECT * FROM TS_QUALITY_REPORT('stats', 30);
