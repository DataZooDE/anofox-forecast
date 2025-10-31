-- Detect
SELECT * FROM sales_stats WHERE is_constant = true;

-- Fix
SELECT * FROM TS_DROP_CONSTANT('sales', product_id, sales_amount);
