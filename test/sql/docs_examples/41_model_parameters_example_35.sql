-- Compare multiple models for same data
WITH forecasts AS (
    SELECT 'Naive' AS model, TS_FORECAST(date, value, 'Naive', 12, MAP{}) AS fc FROM data
    UNION ALL
    SELECT 'SMA-5', TS_FORECAST(date, value, 'SMA', 12, MAP{'window': 5}) FROM data
    UNION ALL
    SELECT 'SMA-10', TS_FORECAST(date, value, 'SMA', 12, MAP{'window': 10}) FROM data
    UNION ALL
    SELECT 'SES', TS_FORECAST(date, value, 'SES', 12, MAP{'alpha': 0.3}) FROM data
    UNION ALL
    SELECT 'Theta', TS_FORECAST(date, value, 'Theta', 12, MAP{}) FROM data
    UNION ALL
    SELECT 'AutoETS', TS_FORECAST(date, value, 'AutoETS', 12, MAP{'season_length': 1}) FROM data
)
SELECT model, UNNEST(fc.point_forecast) AS forecast, UNNEST(fc.forecast_step) AS step
FROM forecasts;
