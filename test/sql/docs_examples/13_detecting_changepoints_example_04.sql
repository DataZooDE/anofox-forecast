-- Create sample sales data
CREATE TABLE sales_data AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Detect changepoints for each product
SELECT 
    product_id,
    date_col AS date,
    is_changepoint
FROM TS_DETECT_CHANGEPOINTS_BY('sales_data', product_id, date, sales, MAP{})
WHERE is_changepoint = true
ORDER BY product_id, date_col;

-- Count changepoints per product
SELECT 
    product_id,
    COUNT(*) FILTER (WHERE is_changepoint) AS num_changepoints
FROM TS_DETECT_CHANGEPOINTS_BY('sales_data', product_id, date, sales, MAP{})
GROUP BY product_id
ORDER BY num_changepoints DESC;
