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
    SELECT * FROM anofox_fcst_ts_forecast('train_data', date, sales_amount, 'Theta', 12, MAP{})
),
predictions AS (
    SELECT LIST(point_forecast ORDER BY forecast_step) AS pred FROM forecast
),
actuals AS (
    SELECT LIST(value ORDER BY date) AS actual FROM test_data
)
SELECT 
    anofox_fcst_ts_mae(actual, pred) AS mae,
    anofox_fcst_ts_rmse(actual, pred) AS rmse,
    anofox_fcst_ts_mape(actual, pred) AS mape_percent
FROM actuals, predictions;
