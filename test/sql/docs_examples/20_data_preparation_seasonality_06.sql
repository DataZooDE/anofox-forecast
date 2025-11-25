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
