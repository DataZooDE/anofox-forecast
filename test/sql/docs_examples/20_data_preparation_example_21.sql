-- Create sample multi-product data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.1 THEN NULL
        ELSE 100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10)
    END AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_nulls_mean('sales', product_id, date, sales_amount);
