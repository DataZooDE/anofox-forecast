CREATE TABLE sales_filled AS
SELECT * FROM TS_FILL_GAPS('sales', product_id, date, sales_amount);

-- Before: [2023-01-01, 2023-01-03, 2023-01-05]
-- After:  [2023-01-01, 2023-01-02, 2023-01-03, 2023-01-04, 2023-01-05]
--         values:      [100, NULL, 150, NULL, 200]
