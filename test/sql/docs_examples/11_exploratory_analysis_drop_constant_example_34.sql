-- Detect constant series
SELECT * FROM sales_stats WHERE is_constant = true;

-- Remove constant series
CREATE TABLE sales_no_constant AS
SELECT * FROM TS_DROP_CONSTANT('sales', product_id, sales_amount);

