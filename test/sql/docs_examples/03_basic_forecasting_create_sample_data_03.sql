-- Automatically detect seasonal periods
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales_complete', product_id, date, amount);

-- Result:
-- | product_id | detected_periods | primary_period | is_seasonal |
-- |------------|------------------|----------------|-------------|
-- | P001       | [7, 30]          | 7              | true        |
