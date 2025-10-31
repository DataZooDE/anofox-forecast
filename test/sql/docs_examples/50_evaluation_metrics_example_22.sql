WITH theta_fc AS (
    SELECT TS_FORECAST(date, value, 'Theta', 10, MAP{}) AS fc FROM train
),
naive_fc AS (
    SELECT TS_FORECAST(date, value, 'Naive', 10, MAP{}) AS fc FROM train
),
actuals AS (
    SELECT LIST(value) AS actual FROM test LIMIT 10
)
SELECT 
    TS_MASE(actual, theta_fc.fc.point_forecast, naive_fc.fc.point_forecast) AS mase,
    CASE 
        WHEN TS_MASE(actual, theta_fc.fc.point_forecast, naive_fc.fc.point_forecast) < 1.0
        THEN 'Theta beats Naive ✅'
        ELSE 'Naive is better'
    END AS result
FROM theta_fc, naive_fc, actuals;
