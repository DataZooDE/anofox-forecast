-- Create sample sales data with trailing zeros
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN d > 80 THEN 0.0  -- Trailing zeros
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id);

-- Remove trailing zeros
CREATE TABLE sales_no_trailing_zeros AS
SELECT * FROM anofox_fcst_ts_drop_trailing_zeros('sales', product_id, date, sales_amount);

-- Verify result (should end at day 80)
SELECT 
    product_id,
    MAX(date) AS last_date,
    COUNT(*) AS remaining_days
FROM sales_no_trailing_zeros
GROUP BY product_id;
