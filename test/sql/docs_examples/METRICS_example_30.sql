-- Never evaluate on training data!
WITH split AS (
    SELECT * FROM data WHERE date < '2024-01-01'  -- Train
),
forecast AS (
    SELECT TS_FORECAST(date, value, 'AutoETS', 30, MAP{}) AS fc FROM split
),
test AS (
    SELECT LIST(value) AS actual 
    FROM data 
    WHERE date >= '2024-01-01'  -- Test (holdout)
    LIMIT 30
)
SELECT 
    TS_MAE(actual, forecast.fc.point_forecast) AS test_mae
FROM test, forecast;
