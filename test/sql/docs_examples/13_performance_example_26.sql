-- Automatically uses all cores
SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, {...});
