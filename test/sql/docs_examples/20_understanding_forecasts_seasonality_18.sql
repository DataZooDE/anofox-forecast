-- Remove trend via differencing
CREATE TABLE sales_diff AS
WITH series AS (
    SELECT 
        product_id,
        date,
        amount,
        amount - LAG(amount) OVER (PARTITION BY product_id ORDER BY date) AS diff_amount
    FROM sales
)
SELECT * FROM series WHERE diff_amount IS NOT NULL;

-- Check if stationarity improved
SELECT 
    'Original' AS data,
    STDDEV(amount) / AVG(amount) AS cv
FROM sales
UNION ALL
SELECT 
    'Differenced',
    STDDEV(diff_amount) / AVG(ABS(diff_amount))
FROM sales_diff;

-- Lower CV = more stationary
