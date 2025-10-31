-- Generate business recommendations based on forecasts
CREATE VIEW revenue_recommendations AS
WITH forecast_analysis AS (
    SELECT 
        product_id,
        SUM(point_forecast) AS next_30d_revenue,
        AVG(point_forecast) AS avg_daily_revenue,
        STDDEV(point_forecast) AS revenue_volatility
    FROM product_revenue_forecast
    WHERE date_col <= CURRENT_DATE + INTERVAL '30 days'
    GROUP BY product_id
),
historical_avg AS (
    SELECT 
        product_id,
        AVG(revenue) AS hist_avg_daily
    FROM product_sales_clean
    WHERE date >= CURRENT_DATE - INTERVAL '90 days'
    GROUP BY product_id
),
analysis AS (
    SELECT 
        f.product_id,
        f.next_30d_revenue,
        f.avg_daily_revenue,
        h.hist_avg_daily,
        f.revenue_volatility,
        ROUND(100.0 * (f.avg_daily_revenue - h.hist_avg_daily) / h.hist_avg_daily, 1) AS growth_pct
    FROM forecast_analysis f
    JOIN historical_avg h ON f.product_id = h.product_id
)
SELECT 
    product_id,
    ROUND(next_30d_revenue, 0) AS next_30d_revenue,
    growth_pct || '%' AS growth_vs_hist,
    CASE 
        WHEN growth_pct > 20 THEN '🚀 Scale up: Increase inventory and marketing'
        WHEN growth_pct > 10 THEN '📈 Growing: Maintain current strategy'
        WHEN growth_pct > -10 THEN '↔️ Stable: Monitor closely'
        WHEN growth_pct > -20 THEN '📉 Declining: Investigate causes'
        ELSE '🔴 Trouble: Consider discontinuation or promotion'
    END AS recommendation
FROM analysis
ORDER BY growth_pct DESC;
