-- Create sample data
CREATE TABLE data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 89) t(d);  -- 90 days of data

-- Never evaluate on training data!
WITH split AS (
    SELECT * FROM data WHERE date < DATE '2024-01-01'  -- Train
),
forecast AS (
    SELECT LIST(point_forecast ORDER BY forecast_step) AS pred FROM anofox_fcst_ts_forecast('split', date, value, 'AutoETS', 30, MAP{})
),
test AS (
    SELECT LIST(value ORDER BY date) AS actual 
    FROM data 
    WHERE date >= DATE '2024-01-01'  -- Test (holdout)
    LIMIT 30
)
SELECT 
    anofox_fcst_ts_mae(actual, pred) AS test_mae
FROM test, forecast;
