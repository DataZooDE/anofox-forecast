SELECT * FROM TS_DETECT_PLATEAUS('sales', product_id, date, sales_amount);

-- Returns:
-- | series_id | max_plateau_size | max_plateau_nonzero | max_plateau_zeros | n_plateaus |
-- |-----------|------------------|---------------------|-------------------|------------|
-- | P001      | 45               | 12                  | 45                | 8          |
