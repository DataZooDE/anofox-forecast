-- Example: Inventory planning accepts 5% error
WITH quality_check AS (
    SELECT 
        product_id,
        TS_MAPE(actual, predicted) AS mape
    FROM forecast_evaluation
)
SELECT 
    product_id,
    mape,
    CASE 
        WHEN mape < 5.0 THEN 'Use forecast'
        WHEN mape < 10.0 THEN 'Use with caution'
        ELSE 'Manual review required'
    END AS recommendation
FROM quality_check;
