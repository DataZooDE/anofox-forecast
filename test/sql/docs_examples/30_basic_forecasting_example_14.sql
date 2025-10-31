-- Check data quality before forecasting
CREATE TABLE stats AS SELECT * FROM TS_STATS('sales', product_id, date, amount);

-- Look for problems
SELECT * FROM TS_GET_PROBLEMATIC('stats', 0.7);
