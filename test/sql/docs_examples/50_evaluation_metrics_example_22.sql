-- Create sample training data
CREATE TABLE train AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(0, 60) t(d);  -- 61 days

-- Create test data
CREATE TABLE test AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS value
FROM generate_series(61, 70) t(d);

WITH theta_fc AS (
    SELECT LIST(point_forecast ORDER BY forecast_step) AS pred FROM TS_FORECAST('train', date, value, 'Theta', 10, MAP{})
),
naive_fc AS (
    SELECT LIST(point_forecast ORDER BY forecast_step) AS baseline FROM TS_FORECAST('train', date, value, 'Naive', 10, MAP{})
),
actuals AS (
    SELECT LIST(value ORDER BY date) AS actual FROM test
)
SELECT 
    TS_MASE(actual, pred, baseline) AS mase,
    CASE 
        WHEN TS_MASE(actual, pred, baseline) < 1.0
        THEN 'Theta beats Naive ✅'
        ELSE 'Naive is better'
    END AS result
FROM theta_fc, naive_fc, actuals;
