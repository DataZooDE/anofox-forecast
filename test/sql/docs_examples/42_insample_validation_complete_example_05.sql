-- Get fitted values and compute residuals
WITH forecast_data AS (
    SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 14,
                              {'return_insample': true, 'seasonal_period': 7})
),
residuals AS (
    SELECT 
        UNNEST(insample_fitted) AS fitted,
        ROW_NUMBER() OVER () AS idx
    FROM forecast_data
),
actuals AS (
    SELECT 
        amount AS actual,
        ROW_NUMBER() OVER (ORDER BY date) AS idx
    FROM sales
)
SELECT 
    r.idx AS observation,
    a.actual,
    ROUND(r.fitted, 2) AS fitted,
    ROUND(a.actual - r.fitted, 2) AS residual,
    CASE 
        WHEN ABS(a.actual - r.fitted) > 2 * STDDEV(a.actual - r.fitted) OVER ()
        THEN '⚠️ Outlier'
        ELSE '✓'
    END AS flag
FROM residuals r
JOIN actuals a ON r.idx = a.idx
ORDER BY ABS(a.actual - r.fitted) DESC
LIMIT 10;
