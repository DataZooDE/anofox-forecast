-- Compute R² on training data
WITH fitted AS (
    SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 7,
                              {'return_insample': true, 'seasonal_period': 7})
),
fit_quality AS (
    SELECT 
        TS_R2(LIST(s.amount), f.insample_fitted) AS r_squared
    FROM sales s, fitted f
)
SELECT 
    ROUND(r_squared, 4) AS r_squared,
    CASE 
        WHEN r_squared > 0.9 THEN '🌟 Excellent fit'
        WHEN r_squared > 0.7 THEN '✅ Good fit'
        WHEN r_squared > 0.5 THEN '⚠️ Moderate fit'
        ELSE '❌ Poor fit - try different model'
    END AS assessment
FROM fit_quality;
