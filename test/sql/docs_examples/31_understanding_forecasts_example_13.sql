-- Compare AutoETS vs Naive (baseline)
WITH ets_forecast AS (
    SELECT 'AutoETS' AS model, * 
    FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7})
),
naive_forecast AS (
    SELECT 'Naive' AS model, *
    FROM TS_FORECAST('sales', date, amount, 'Naive', 28, MAP{})
),
evaluation AS (
    SELECT 
        model,
        TS_MAE(LIST(actual), LIST(forecast)) AS mae
    FROM results
    GROUP BY model
)
SELECT 
    model,
    ROUND(mae, 2) AS mae,
    ROUND(100 * (1 - mae / MAX(mae) OVER ()), 1) AS improvement_pct
FROM evaluation
ORDER BY mae;
