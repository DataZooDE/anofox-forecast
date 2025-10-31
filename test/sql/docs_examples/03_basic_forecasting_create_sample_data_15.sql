-- Don't guess - detect!
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales', product_id, date, amount);

-- Use detected period in forecast
WITH seasonality AS (
    SELECT product_id, primary_period
    FROM TS_DETECT_SEASONALITY_ALL('sales', product_id, date, amount)
)
SELECT f.*
FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28,
                    {'seasonal_period': (SELECT primary_period FROM seasonality WHERE product_id = s.product_id)}) f;
