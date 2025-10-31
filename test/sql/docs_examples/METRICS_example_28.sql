-- Quantify improvement over naive forecast
WITH model_fc AS (
    SELECT TS_FORECAST(date, value, 'AutoETS', 12, MAP{}) AS fc FROM train
),
naive_fc AS (
    SELECT TS_FORECAST(date, value, 'Naive', 12, MAP{}) AS fc FROM train
),
actuals AS (
    SELECT LIST(value) AS actual FROM test LIMIT 12
)
SELECT 
    TS_MASE(actual, model_fc.fc.point_forecast, naive_fc.fc.point_forecast) AS mase,
    ROUND((1.0 - TS_MASE(actual, model_fc.fc.point_forecast, naive_fc.fc.point_forecast)) * 100, 1) 
        AS improvement_percent
FROM actuals, model_fc, naive_fc;

-- Example: MASE=0.35 → 65% improvement over naive
