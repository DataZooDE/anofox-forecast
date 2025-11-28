-- Create sample stats table
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

CREATE TABLE stats AS
SELECT * FROM TS_STATS('sales', product_id, date, sales_amount, '1d');

-- Overall statistics
SELECT * FROM TS_STATS_SUMMARY('stats');

-- Example output:
-- | total_series | total_observations | avg_series_length | date_span | frequency |
-- |--------------|-------------------|------------------|-----------|-----------|
-- | 1000         | 365000            | 365.0             | 730       | Daily     |
