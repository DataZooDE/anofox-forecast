-- Enable timing
.timer on

-- Run query
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, {...});

-- Check execution time
-- Run Time: real 12.345 seconds
