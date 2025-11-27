-- Create sample sales data with leading zeros
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN d < 10 THEN 0.0  -- Leading zeros
        ELSE 100 + 50 * SIN(2 * PI() * (d - 10) / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id);

-- Remove leading zeros
CREATE TABLE sales_no_leading_zeros AS
SELECT * FROM TS_DROP_LEADING_ZEROS('sales', product_id, date, sales_amount);

-- Verify result (should start from day 10)
SELECT 
    product_id,
    MIN(date) AS first_date,
    COUNT(*) AS remaining_days
FROM sales_no_leading_zeros
GROUP BY product_id;
