-- Create sample sales and stats
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales', product_id, date, sales_amount, '1d');

-- Create prepared sales data
CREATE TABLE sales_prepared AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM TS_FILL_GAPS('sales', product_id, date, sales_amount, '1d');

-- Only forecast high-quality series
WITH quality_check AS (
    SELECT series_id
    FROM sales_stats
    WHERE length >= 60                -- Sufficient history
      AND n_unique_values > 5         -- Not near-constant
      AND is_constant = false         -- Not constant
)
SELECT s.*
FROM sales_prepared s
WHERE s.product_id::VARCHAR IN (SELECT series_id::VARCHAR FROM quality_check);
