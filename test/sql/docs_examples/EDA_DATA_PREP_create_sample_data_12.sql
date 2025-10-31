CREATE TABLE sales_long_enough AS
SELECT * FROM TS_DROP_SHORT('sales', product_id, date, 30);  -- min 30 obs
