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
FROM generate_series(61, 72) t(d);

-- Quantify improvement over naive forecast
WITH model_fc AS (
    SELECT LIST(point_forecast ORDER BY forecast_step) AS pred FROM anofox_fcst_ts_forecast('train', date, value, 'AutoETS', 12, MAP{})
),
naive_fc AS (
    SELECT LIST(point_forecast ORDER BY forecast_step) AS baseline FROM anofox_fcst_ts_forecast('train', date, value, 'Naive', 12, MAP{})
),
actuals AS (
    SELECT LIST(value ORDER BY date) AS actual FROM test
)
SELECT 
    anofox_fcst_ts_mase(actual, pred, baseline) AS mase,
    ROUND((1.0 - anofox_fcst_ts_mase(actual, pred, baseline)) * 100, 1) AS improvement_percent
FROM actuals, model_fc, naive_fc;

-- Example: MASE=0.35 → 65% improvement over naive
