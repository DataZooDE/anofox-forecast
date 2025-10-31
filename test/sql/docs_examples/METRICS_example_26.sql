-- Alert if forecast quality degrades
CREATE OR REPLACE TABLE forecast_quality_log AS
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
