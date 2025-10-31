-- Check residual properties
WITH residuals AS (
    SELECT actual - fitted AS resid
    FROM actuals_fitted
)
SELECT 
    ROUND(AVG(resid), 4) AS mean_residual,  -- Should be ~0
    ROUND(STDDEV(resid), 2) AS std_residual,
    ROUND(MIN(resid), 2) AS min_residual,
    ROUND(MAX(resid), 2) AS max_residual,
    COUNT(CASE WHEN ABS(resid) > 2 * STDDEV(resid) OVER () THEN 1 END) AS n_outliers
FROM residuals;

-- Good model: mean_residual ≈ 0, n_outliers < 5%
