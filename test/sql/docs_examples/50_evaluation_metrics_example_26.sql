-- Create sample daily evaluation data
CREATE TABLE daily_evaluation AS
SELECT 
    CURRENT_DATE AS date,
    [100.0, 102.0, 98.0, 105.0]::DOUBLE[] AS actual_list,
    [101.0, 103.0, 99.0, 106.0]::DOUBLE[] AS forecast_list;

-- Alert if forecast quality degrades
CREATE TABLE forecast_quality_log AS
SELECT 
    CURRENT_DATE AS date,
    TS_MAE(actual_list, forecast_list) AS mae,
    TS_MAPE(actual_list, forecast_list) AS mape
FROM daily_evaluation;

-- Check if quality is degrading
SELECT 
    CASE 
        WHEN mape > 15 THEN 'CRITICAL: Retrain model'
        WHEN mape > 10 THEN 'WARNING: Monitor closely'
        WHEN mape > 5 THEN 'CAUTION: Check for drift'
        ELSE 'OK'
    END AS alert_level
FROM forecast_quality_log
WHERE date = CURRENT_DATE;
