-- Fill NULLs with 0
CREATE TABLE sales_filled_zero AS
SELECT * FROM TS_FILL_NULLS_CONST('sales', product_id, date, sales_amount, 0.0);

-- Fill NULLs with a specific value (e.g., -1 for missing data indicator)
CREATE TABLE sales_filled_marker AS
SELECT * FROM TS_FILL_NULLS_CONST('sales', product_id, date, sales_amount, -1.0);

