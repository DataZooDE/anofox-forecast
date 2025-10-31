-- Assess model fit quality
WITH fc AS (
    SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28,
                              {'return_insample': true, 'seasonal_period': 7})
),
fitted_vs_actual AS (
    SELECT 
        s.amount AS actual,
        UNNEST(fc.insample_fitted) AS fitted
    FROM sales s, fc
)
SELECT 
    'Training observations: ' || COUNT(*) AS metric
FROM fitted_vs_actual
UNION ALL
SELECT 
    'R-squared: ' || ROUND(TS_R2(LIST(actual), LIST(fitted)), 4)
FROM fitted_vs_actual
UNION ALL
SELECT 
    'RMSE: ' || ROUND(TS_RMSE(LIST(actual), LIST(fitted)), 2)
FROM fitted_vs_actual
UNION ALL
SELECT 
    'MAE: ' || ROUND(TS_MAE(LIST(actual), LIST(fitted)), 2)
FROM fitted_vs_actual;
