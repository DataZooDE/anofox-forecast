-- Create sample training data
CREATE TABLE train_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 60) t(d);

-- Create sample test data (matching forecast horizon of 12)
CREATE TABLE test_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(61, 72) t(d);

WITH forecast AS (
    SELECT * FROM TS_FORECAST('train_data', date, sales_amount, 'Theta', 12, MAP{})
),
predictions AS (
    SELECT LIST(point_forecast ORDER BY forecast_step) AS pred FROM forecast
),
actuals AS (
    SELECT LIST(value ORDER BY date) AS actual FROM test_data
)
SELECT 
    TS_MAE(actual, pred) AS mae,
    TS_RMSE(actual, pred) AS rmse,
    TS_MAPE(actual, pred) AS mape_percent
FROM actuals, predictions;
