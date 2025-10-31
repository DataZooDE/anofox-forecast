-- Check if residuals are random (good) or patterned (bad)
WITH residuals AS (
    SELECT 
        actual - fitted AS resid,
        LAG(actual - fitted) OVER (ORDER BY date) AS lag1_resid
    FROM actuals_fitted
)
SELECT 
    ROUND(CORR(resid, lag1_resid), 4) AS lag1_autocorrelation,
    CASE 
        WHEN ABS(CORR(resid, lag1_resid)) < 0.2 THEN '✅ Random (good)'
        ELSE '⚠️ Pattern detected (model may be missing something)'
    END AS assessment
FROM residuals;
