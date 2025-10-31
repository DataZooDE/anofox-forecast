CREATE TABLE sales_complete AS
SELECT * FROM TS_DROP_GAPPY('sales', product_id, date, 0.10);  -- max 10% gaps
