SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales', product_id, date, sales_amount);

-- Returns:
-- | series_id | detected_periods | primary_period | is_seasonal |
-- |-----------|------------------|----------------|-------------|
-- | P001      | [7, 30]          | 7              | true        |
-- | P002      | []               | NULL           | false       |
