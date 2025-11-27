-- Create sample sales data with NULL values
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.15 THEN NULL  -- 15% missing
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id);

-- Backward fill (use next known value)
CREATE TABLE sales_backward_filled AS
SELECT * FROM TS_FILL_NULLS_BACKWARD('sales', product_id, date, sales_amount);

-- Verify results (should have no NULLs)
SELECT 
    product_id,
    COUNT(*) AS total_rows,
    COUNT(sales_amount) AS non_null_rows
FROM sales_backward_filled
GROUP BY product_id;
