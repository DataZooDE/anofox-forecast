-- Check if your data has sufficient seasonality for seasonal models
WITH analysis AS (
    SELECT TS_ANALYZE_SEASONALITY(
        LIST(date ORDER BY date),
        LIST(sales ORDER BY date)
    ) AS result
    FROM sales_data
)
SELECT 
    CASE 
        WHEN result.seasonal_strength > 0.6 THEN 'Use seasonal models'
        WHEN result.seasonal_strength > 0.3 THEN 'Consider seasonal models'
        ELSE 'Use non-seasonal models'
    END AS recommendation,
    result.primary_period AS detected_period,
    ROUND(result.seasonal_strength, 3) AS strength
FROM analysis;
