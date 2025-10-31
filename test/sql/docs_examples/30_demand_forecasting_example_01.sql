-- Your sales data should have:
-- 1. Product identifier
-- 2. Date
-- 3. Quantity sold

CREATE TABLE product_sales AS
SELECT 
    sku,
    sale_date,
    quantity_sold
FROM your_sales_table
WHERE sale_date >= CURRENT_DATE - INTERVAL '2 years';  -- 2 years history
