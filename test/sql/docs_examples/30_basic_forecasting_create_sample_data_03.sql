-- Automatically detect seasonal periods
SELECT 
    product_id,
    TS_DETECT_SEASONALITY(LIST(amount ORDER BY date)) AS detected_periods
FROM sales_complete
GROUP BY product_id;

-- Result:
-- | product_id | detected_periods | primary_period | is_seasonal |
-- |------------|------------------|----------------|-------------|
-- | P001       | [7, 30]          | 7              | true        |
