-- Fill time gaps
CREATE TABLE sales_filled AS
SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, amount);

-- Remove constant series
CREATE TABLE sales_clean AS
SELECT * FROM TS_DROP_CONSTANT('sales_filled', product_id, amount);

-- Fill missing values
CREATE TABLE sales_complete AS
SELECT * FROM TS_FILL_NULLS_FORWARD('sales_clean', product_id, date, amount);
