-- Try 3 models and compare
WITH ets AS (
    SELECT 'AutoETS' AS model, * 
    FROM TS_FORECAST('sales', date, amount, 'AutoETS', 14, {'seasonal_period': 7})
),
theta AS (
    SELECT 'Theta' AS model, * 
    FROM TS_FORECAST('sales', date, amount, 'Theta', 14, {'seasonal_period': 7})
),
naive AS (
    SELECT 'SeasonalNaive' AS model, * 
    FROM TS_FORECAST('sales', date, amount, 'SeasonalNaive', 14, {'seasonal_period': 7})
)
SELECT * FROM ets 
UNION ALL SELECT * FROM theta 
UNION ALL SELECT * FROM naive
ORDER BY model, forecast_step;
