-- Create sample sales data with some short series
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20) AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id)
WHERE 
    (product_id = 'P001' AND d <= 10) OR  -- Short series (11 days)
    (product_id = 'P002' AND d <= 20) OR  -- Short series (21 days)
    (product_id = 'P003' AND d <= 90);    -- Long series (91 days)

-- Remove series with less than 30 observations
CREATE TABLE sales_long_enough AS
SELECT * FROM TS_DROP_SHORT('sales', product_id, date, 30);

-- Verify result
SELECT DISTINCT product_id FROM sales_long_enough;
