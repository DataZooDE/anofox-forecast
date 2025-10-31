EXPLAIN ANALYZE
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7});
