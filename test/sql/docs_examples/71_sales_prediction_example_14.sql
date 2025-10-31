-- Compare actual to forecast to target
CREATE VIEW performance_dashboard AS
WITH daily_actuals AS (
    SELECT 
        date,
        SUM(revenue) AS actual_revenue
    FROM sales
    WHERE date >= DATE_TRUNC('month', CURRENT_DATE)
    GROUP BY date
),
daily_forecasts AS (
    SELECT 
        date_col AS date,
        SUM(point_forecast) AS forecasted_revenue
    FROM product_revenue_forecast
    WHERE date_col >= DATE_TRUNC('month', CURRENT_DATE)
    GROUP BY date_col
),
daily_targets AS (
    SELECT 
        date,
        target_revenue
    FROM daily_revenue_targets
    WHERE date >= DATE_TRUNC('month', CURRENT_DATE)
)
SELECT 
    COALESCE(a.date, f.date, t.date) AS date,
    a.actual_revenue,
    f.forecasted_revenue,
    t.target_revenue,
    ROUND(100.0 * a.actual_revenue / NULLIF(f.forecasted_revenue, 0), 1) AS pct_of_forecast,
    ROUND(100.0 * a.actual_revenue / NULLIF(t.target_revenue, 0), 1) AS pct_of_target,
    CASE 
        WHEN a.actual_revenue >= t.target_revenue THEN '🌟 Beat target'
        WHEN a.actual_revenue >= f.forecasted_revenue THEN '✅ Above forecast'
        WHEN a.actual_revenue >= f.forecasted_revenue * 0.95 THEN '⚠️ Slightly below'
        ELSE '🔴 Underperforming'
    END AS status
FROM daily_actuals a
FULL OUTER JOIN daily_forecasts f ON a.date = f.date
FULL OUTER JOIN daily_targets t ON a.date = t.date
WHERE a.date IS NOT NULL OR f.date >= CURRENT_DATE
ORDER BY date;
