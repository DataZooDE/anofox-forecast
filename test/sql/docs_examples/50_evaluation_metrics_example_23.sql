-- Create sample historical data
CREATE TABLE historical_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 89) t(d);

-- Create sample test data (matching forecast horizon of 7)
CREATE TABLE test_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(90, 96) t(d);

-- Evaluate forecast quality over time windows
WITH forecast_all AS (
    SELECT LIST(point_forecast ORDER BY forecast_step) AS pred FROM anofox_fcst_ts_forecast('historical_data', date, value, 'AutoETS', 7, MAP{'seasonal_period': 7})
),
test_actuals AS (
    SELECT LIST(value ORDER BY date) AS actual FROM test_data LIMIT 7
)
SELECT 
    ROUND(anofox_fcst_ts_mae(actual, pred), 2) AS mae,
    ROUND(anofox_fcst_ts_mape(actual, pred), 2) AS mape
FROM test_actuals, forecast_all;
