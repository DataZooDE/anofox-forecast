-- Create sample sales data with constant series
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN product_id = 'P001' THEN 100.0  -- Constant series
        WHEN product_id = 'P002' THEN 50.0 + 10 * SIN(2 * PI() * d / 7)  -- Variable series
        ELSE 75.0  -- Another constant series
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Generate statistics
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales', product_id, date, sales_amount);

-- Detect constant series
SELECT * FROM sales_stats WHERE is_constant = true;

-- Fix: Remove constant series
SELECT * FROM TS_DROP_CONSTANT('sales', product_id, sales_amount);
