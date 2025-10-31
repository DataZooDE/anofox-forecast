-- Is your sales pipeline healthy?
WITH pipeline_forecast AS (
    SELECT 
        DATE_TRUNC('month', date_col) AS month,
        SUM(point_forecast) AS monthly_forecast
    FROM product_revenue_forecast
    GROUP BY month
),
targets AS (
    SELECT month, target_revenue
    FROM monthly_targets
)
SELECT 
    f.month,
    ROUND(f.monthly_forecast, 0) AS forecast,
    ROUND(t.target_revenue, 0) AS target,
    ROUND(f.monthly_forecast - t.target_revenue, 0) AS gap,
    ROUND(100.0 * f.monthly_forecast / t.target_revenue, 1) || '%' AS attainment_pct,
    CASE 
        WHEN f.monthly_forecast >= t.target_revenue * 1.05 THEN '🌟 Exceeding target'
        WHEN f.monthly_forecast >= t.target_revenue THEN '✅ On track'
        WHEN f.monthly_forecast >= t.target_revenue * 0.95 THEN '⚠️ Slightly behind'
        ELSE '🔴 Significant gap'
    END AS status
FROM pipeline_forecast f
JOIN targets t ON f.month = t.month
ORDER BY f.month;
