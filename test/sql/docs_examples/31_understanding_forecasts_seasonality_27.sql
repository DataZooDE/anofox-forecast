-- Check if variance changes over time
WITH residuals AS (
    SELECT 
        NTILE(4) OVER (ORDER BY date) AS quartile,
        actual - fitted AS resid
    FROM actuals_fitted
)
SELECT 
    quartile,
    ROUND(VARIANCE(resid), 2) AS variance,
    COUNT(*) AS n_obs
FROM residuals
GROUP BY quartile;

-- Variance should be similar across quartiles
