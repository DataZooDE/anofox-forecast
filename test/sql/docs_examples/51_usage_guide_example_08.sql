-- Compare Naive vs SMA forecasts
WITH naive_forecast AS (
    SELECT forecast_step, point_forecast as naive_pred
    FROM FORECAST('date', 'amount', 'Naive', 5, NULL)
),
sma_forecast AS (
    SELECT forecast_step, point_forecast as sma_pred
    FROM FORECAST('date', 'amount', 'SMA', 5, NULL)
)
SELECT 
    naive_forecast.forecast_step,
    naive_pred,
    sma_pred,
    abs(naive_pred - sma_pred) as difference
FROM naive_forecast
JOIN sma_forecast USING (forecast_step)
ORDER BY forecast_step;
