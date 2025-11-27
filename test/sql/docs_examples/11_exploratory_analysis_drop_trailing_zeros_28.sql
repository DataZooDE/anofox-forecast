-- Remove trailing zeros
CREATE TABLE sales_no_trailing_zeros AS
SELECT * FROM TS_DROP_TRAILING_ZEROS('sales', product_id, date, sales_amount);

