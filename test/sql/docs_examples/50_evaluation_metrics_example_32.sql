-- Create sample forecast evaluation data
CREATE TABLE forecast_evaluation AS
SELECT 
    1 AS step,
    100.0 AS actual,
    102.5 AS predicted
UNION ALL
SELECT 2, 105.0, 104.0
UNION ALL
SELECT 3, 103.0, 105.5;

-- Example: Inventory planning accepts 5% error
WITH quality_check AS (
    SELECT 
        anofox_fcst_ts_mape(LIST(actual ORDER BY step), LIST(predicted ORDER BY step)) AS mape
    FROM forecast_evaluation
)
SELECT 
    mape,
    CASE 
        WHEN mape < 5.0 THEN 'Use forecast'
        WHEN mape < 10.0 THEN 'Use with caution'
        ELSE 'Manual review required'
    END AS recommendation
FROM quality_check;
