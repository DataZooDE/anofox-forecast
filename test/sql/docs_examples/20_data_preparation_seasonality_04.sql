SELECT * FROM TS_ANALYZE_ZEROS('sales', product_id, date, sales_amount);

-- Returns:
-- | series_id | n_leading_zeros | n_trailing_zeros | total_edge_zeros |
-- |-----------|-----------------|------------------|------------------|
-- | P001      | 12              | 8                | 20               |
