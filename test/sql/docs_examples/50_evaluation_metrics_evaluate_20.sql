WITH forecast AS (
    SELECT TS_FORECAST(date, value, 'Theta', 12, MAP{}) AS fc
    FROM train_data
),
predictions AS (
    SELECT fc.point_forecast AS pred FROM forecast
),
actuals AS (
    SELECT LIST(value) AS actual FROM test_data
)
SELECT 
    TS_MAE(actual, pred) AS mae,
    TS_RMSE(actual, pred) AS rmse,
    TS_MAPE(actual, pred) AS mape_percent
FROM actuals, predictions;
