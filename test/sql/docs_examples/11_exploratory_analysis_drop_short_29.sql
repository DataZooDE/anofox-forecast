-- Remove series with less than 30 observations
CREATE TABLE sales_long_enough AS
SELECT * FROM TS_DROP_SHORT('sales', product_id, date, 30);

