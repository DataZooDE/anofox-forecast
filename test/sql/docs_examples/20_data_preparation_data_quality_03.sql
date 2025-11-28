-- Create sample multi-product data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + (ROW_NUMBER() OVER (PARTITION BY product_id ORDER BY product_id) % 3 + 1) * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Create stats table first
CREATE TABLE stats AS
SELECT * FROM TS_STATS('sales', product_id, date, sales_amount, '1d');

-- All checks in one report
SELECT * FROM TS_QUALITY_REPORT('stats', 30);

-- Example output:
-- | check_type              | total_series | series_with_gaps | pct_with_gaps |
-- |-------------------------|--------------|------------------|---------------|
-- | Gap Analysis            | 1000         | 150              | 15.0%         |
-- | Missing Values          | 1000         | 45               | 4.5%          |
-- | Constant Series         | 1000         | 23               | 2.3%          |
-- | Short Series (< 30)     | 1000         | 67               | 6.7%          |
-- | End Date Alignment      | 1000         | 892              | 11 rows       |
