-- Remove leading zeros
CREATE TABLE sales_no_leading_zeros AS
SELECT * FROM TS_DROP_LEADING_ZEROS('sales', product_id, date, sales_amount);

