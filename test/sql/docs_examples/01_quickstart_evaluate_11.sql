-- Create sample multi-product data
CREATE TABLE my_sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- 1. Check data quality
SELECT * FROM anofox_fcst_ts_stats('my_sales', product_id, date, sales, '1d');

-- 2. Detect seasonality
SELECT 
    product_id,
    anofox_fcst_ts_detect_seasonality(LIST(sales ORDER BY date)) AS detected_periods
FROM my_sales
GROUP BY product_id;

-- 3. Try different models
-- See guides/11_model_selection.md
