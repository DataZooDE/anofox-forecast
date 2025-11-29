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

-- Fill NULLs with 0
CREATE TABLE sales_filled_zero AS
SELECT 
    product_id,
    date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_nulls_const('sales', product_id, date, sales_amount, 0.0);

-- Fill NULLs with a specific value (e.g., -1 for missing data indicator)
CREATE TABLE sales_filled_marker AS
SELECT 
    product_id,
    date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_nulls_const('sales', product_id, date, sales_amount, -1.0);

-- Verify results
SELECT 
    product_id,
    COUNT(*) AS total_rows,
    COUNT(sales_amount) AS non_null_rows,
    SUM(CASE WHEN sales_amount = 0.0 THEN 1 ELSE 0 END) AS zero_count
FROM sales_filled_zero
GROUP BY product_id;
