WITH forecasts AS (
    SELECT * FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, {...})
)
SELECT * FROM forecasts WHERE product_id IN ('P001', 'P002', 'P003');
