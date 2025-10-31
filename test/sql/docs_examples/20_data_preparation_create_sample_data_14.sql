CREATE TABLE sales_no_leading AS
SELECT * FROM TS_DROP_LEADING_ZEROS('sales', product_id, date, sales_amount);
