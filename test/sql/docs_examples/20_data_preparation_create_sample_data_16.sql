-- Create sample sales data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

CREATE TABLE sales_no_edge_zeros AS
SELECT * FROM anofox_fcst_ts_drop_edge_zeros('sales', product_id, date, sales_amount);
