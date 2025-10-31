WITH ets AS (
    SELECT forecast_step, point_forecast AS ets_forecast
    FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7})
),
arima AS (
    SELECT forecast_step, point_forecast AS arima_forecast
    FROM TS_FORECAST('sales', date, amount, 'AutoARIMA', 28, {'seasonal_period': 7})
)
SELECT 
    forecast_step,
    ROUND((ets_forecast + arima_forecast) / 2.0, 2) AS ensemble_forecast
FROM ets
JOIN arima USING (forecast_step);
