-- Create sample multi-product data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

SELECT 
    product_id,
    TS_DETECT_SEASONALITY(LIST(sales_amount ORDER BY date)) AS detected_periods
FROM sales
GROUP BY product_id;

-- Returns:
-- | series_id | detected_periods | primary_period | is_seasonal |
-- |-----------|------------------|----------------|-------------|
-- | P001      | [7, 30]          | 7              | true        |
-- | P002      | []               | NULL           | false       |
