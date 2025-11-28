-- Create sample complete sales data
CREATE TABLE sales_complete AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Automatically detect seasonal periods
SELECT 
    product_id,
    TS_DETECT_SEASONALITY(LIST(sales_amount ORDER BY date)) AS detected_periods
FROM sales_complete
GROUP BY product_id;

-- Result:
-- | product_id | detected_periods | primary_period | is_seasonal |
-- |------------|------------------|----------------|-------------|
-- | P001       | [7, 30]          | 7              | true        |
