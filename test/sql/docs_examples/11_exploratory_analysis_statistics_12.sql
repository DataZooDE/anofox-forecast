-- Detect
SELECT * FROM sales_stats WHERE length < 30;

-- Fix: Drop or aggregate
SELECT * FROM TS_DROP_SHORT('sales', product_id, date, 30);

-- Or: Aggregate similar products
WITH aggregated AS (
    SELECT 
        category AS product_id,  -- Aggregate by category
        date,
        SUM(sales_amount) AS sales_amount
    FROM sales
    JOIN product_catalog USING (product_id)
    GROUP BY category, date
)
SELECT * FROM aggregated;
