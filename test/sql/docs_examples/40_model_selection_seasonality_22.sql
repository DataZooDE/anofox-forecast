-- Solution: Remove constant series
CREATE TABLE variable AS
SELECT * FROM TS_DROP_CONSTANT('sales', product_id, amount);
