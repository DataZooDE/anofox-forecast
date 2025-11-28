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
    product_id,
    date,
    value_col AS sales_amount
FROM TS_FILL_NULLS_BACKWARD('sales', product_id, date, sales_amount);
