-- Create sample forecast results
CREATE TABLE results AS
SELECT 
    1 AS forecast_step,
    100.0 AS actual,
    102.5 AS forecast,
    95.0 AS lower,
    110.0 AS upper
UNION ALL
SELECT 2, 105.0, 104.0, 96.0, 112.0
UNION ALL
SELECT 3, 103.0, 105.5, 97.0, 114.0;

-- If you have actual future values
SELECT 
    TS_MAE(LIST(actual), LIST(forecast)) AS mae,
    TS_RMSE(LIST(actual), LIST(forecast)) AS rmse
FROM results;
