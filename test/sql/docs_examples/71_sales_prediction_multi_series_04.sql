-- Top revenue contributors
SELECT 
    product_id,
    SUM(revenue_forecast) AS total_30d_revenue,
    RANK() OVER (ORDER BY SUM(revenue_forecast) DESC) AS revenue_rank,
    ROUND(100.0 * SUM(revenue_forecast) / SUM(SUM(revenue_forecast)) OVER (), 2) AS pct_of_total
FROM product_revenue_forecast
GROUP BY product_id
ORDER BY revenue_rank
LIMIT 10;

-- Growth vs historical
WITH historical_30d AS (
    SELECT 
        product_id,
        SUM(revenue) AS historical_revenue
    FROM product_sales_clean
    WHERE date BETWEEN CURRENT_DATE - INTERVAL '30 days' AND CURRENT_DATE
    GROUP BY product_id
),
forecasted_30d AS (
    SELECT 
        product_id,
        SUM(revenue_forecast) AS forecasted_revenue
    FROM product_revenue_forecast
    GROUP BY product_id
)
SELECT 
    f.product_id,
    ROUND(h.historical_revenue, 0) AS last_30d_actual,
    ROUND(f.forecasted_revenue, 0) AS next_30d_forecast,
    ROUND(100.0 * (f.forecasted_revenue - h.historical_revenue) / h.historical_revenue, 1) AS growth_pct,
    CASE 
        WHEN (f.forecasted_revenue - h.historical_revenue) / h.historical_revenue > 0.10 THEN '📈 Strong growth'
        WHEN (f.forecasted_revenue - h.historical_revenue) / h.historical_revenue > 0 THEN '📊 Moderate growth'
        WHEN (f.forecasted_revenue - h.historical_revenue) / h.historical_revenue > -0.10 THEN '📉 Slight decline'
        ELSE '⚠️ Significant decline'
    END AS trend
FROM forecasted_30d f
JOIN historical_30d h ON f.product_id = h.product_id
ORDER BY growth_pct DESC;
