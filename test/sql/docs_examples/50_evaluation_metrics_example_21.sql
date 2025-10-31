WITH models AS (
    SELECT 'Naive' AS model, 
           TS_FORECAST(date, value, 'Naive', 10, MAP{}) AS fc
    FROM data
    UNION ALL
    SELECT 'Theta',
           TS_FORECAST(date, value, 'Theta', 10, MAP{})
    FROM data
    UNION ALL
    SELECT 'AutoETS',
           TS_FORECAST(date, value, 'AutoETS', 10, MAP{'season_length': 7})
    FROM data
),
actuals AS (
    SELECT LIST(value) AS actual FROM test_data LIMIT 10
)
SELECT 
    model,
    ROUND(TS_MAE(actual, fc.point_forecast), 2) AS mae,
    ROUND(TS_RMSE(actual, fc.point_forecast), 2) AS rmse,
    ROUND(TS_MAPE(actual, fc.point_forecast), 2) AS mape
FROM models, actuals
ORDER BY TS_MAE(actual, fc.point_forecast);
