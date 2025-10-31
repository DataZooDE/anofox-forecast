-- Store frequently-accessed columns together
CREATE TABLE sales_optimized AS
SELECT 
    product_id,
    date,
    amount
FROM sales
ORDER BY product_id, date;  -- Sorted for better compression
