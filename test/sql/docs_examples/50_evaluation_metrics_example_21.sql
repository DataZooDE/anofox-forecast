-- Create sample test data (matching forecast horizon of 10)
CREATE TABLE test_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(90, 99) t(d);

-- Create sample data
CREATE TABLE data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 89) t(d);  -- 90 days of data

WITH naive_fc AS (
    SELECT LIST(point_forecast ORDER BY forecast_step) AS pred FROM TS_FORECAST('data', date, value, 'Naive', 10, MAP{})
),
theta_fc AS (
    SELECT LIST(point_forecast ORDER BY forecast_step) AS pred FROM TS_FORECAST('data', date, value, 'Theta', 10, MAP{})
),
ets_fc AS (
    SELECT LIST(point_forecast ORDER BY forecast_step) AS pred FROM TS_FORECAST('data', date, value, 'AutoETS', 10, MAP{'seasonal_period': 7})
),
actuals AS (
    SELECT LIST(value ORDER BY date) AS actual FROM test_data LIMIT 10
)
SELECT 
    'Naive' AS model,
    ROUND(TS_MAE(actual, pred), 2) AS mae,
    ROUND(TS_RMSE(actual, pred), 2) AS rmse,
    ROUND(TS_MAPE(actual, pred), 2) AS mape
FROM actuals, naive_fc
UNION ALL
SELECT 
    'Theta',
    ROUND(TS_MAE(actual, pred), 2),
    ROUND(TS_RMSE(actual, pred), 2),
    ROUND(TS_MAPE(actual, pred), 2)
FROM actuals, theta_fc
UNION ALL
SELECT 
    'AutoETS',
    ROUND(TS_MAE(actual, pred), 2),
    ROUND(TS_RMSE(actual, pred), 2),
    ROUND(TS_MAPE(actual, pred), 2)
FROM actuals, ets_fc
ORDER BY mae;
