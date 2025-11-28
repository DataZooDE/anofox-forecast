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

-- Generate stats for reference
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales', product_id, date, sales_amount, '1d');

-- Option A: Forward fill (use last known value)
SELECT * FROM TS_FILL_NULLS_FORWARD('sales', product_id, date, sales_amount);

-- Option B: Mean imputation
SELECT * FROM TS_FILL_NULLS_MEAN('sales', product_id, date, sales_amount);

-- Option C: Drop series with too many nulls
WITH clean AS (
    SELECT * FROM sales_stats WHERE n_null < length * 0.05  -- < 5% missing
)
SELECT s.*
FROM sales s
WHERE s.product_id IN (SELECT series_id FROM clean);
