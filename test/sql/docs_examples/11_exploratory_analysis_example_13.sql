-- Create sample sales data with edge zeros
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN d < 10 THEN 0.0  -- Leading zeros
        WHEN d > 80 THEN 0.0  -- Trailing zeros
        ELSE 100 + 50 * SIN(2 * PI() * (d - 10) / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 90) t(d)
CROSS JOIN (VALUES ('P001'), ('P002')) products(product_id);

-- Detect edge zeros
WITH zero_analysis AS (
    SELECT 
        product_id,
        date,
        sales_amount,
        ROW_NUMBER() OVER (PARTITION BY product_id ORDER BY date) AS rn,
        SUM(CASE WHEN sales_amount != 0 THEN 1 ELSE 0 END) 
            OVER (PARTITION BY product_id ORDER BY date) AS nonzero_count
    FROM sales
)
SELECT 
    product_id,
    MIN(CASE WHEN sales_amount != 0 THEN date END) AS first_sale,
    MAX(CASE WHEN sales_amount != 0 THEN date END) AS last_sale,
    COUNT(*) AS total_days,
    SUM(CASE WHEN sales_amount = 0 THEN 1 ELSE 0 END) AS zero_days
FROM zero_analysis
GROUP BY product_id
HAVING zero_days > 0;

-- Fix: Remove edge zeros
SELECT * FROM anofox_fcst_ts_drop_edge_zeros('sales', product_id, date, sales_amount);
