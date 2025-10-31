WITH prepared AS (
    SELECT * FROM TS_FILL_GAPS('sales', product_id, date, amount)
),
cleaned AS (
    SELECT * FROM TS_DROP_CONSTANT('prepared', product_id, amount)
),
forecasts AS (
    SELECT * FROM TS_FORECAST_BY('cleaned', product_id, date, amount, 'AutoETS', 28, {...})
)
SELECT * FROM forecasts;
