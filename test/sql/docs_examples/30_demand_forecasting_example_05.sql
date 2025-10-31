-- Classify products by forecasted revenue
WITH forecast_revenue AS (
    SELECT 
        sku,
        SUM(forecasted_quantity * unit_price) AS forecasted_revenue_30d
    FROM demand_forecast df
    JOIN product_catalog pc ON df.sku = pc.sku
    GROUP BY sku
),
cumulative AS (
    SELECT 
        sku,
        forecasted_revenue_30d,
        SUM(forecasted_revenue_30d) OVER (ORDER BY forecasted_revenue_30d DESC) AS cumulative_revenue,
        SUM(forecasted_revenue_30d) OVER () AS total_revenue,
        ROW_NUMBER() OVER (ORDER BY forecasted_revenue_30d DESC) AS rank
    FROM forecast_revenue
)
SELECT 
    sku,
    ROUND(forecasted_revenue_30d, 2) AS revenue_30d,
    ROUND(100.0 * cumulative_revenue / total_revenue, 2) AS cumulative_pct,
    CASE 
        WHEN cumulative_revenue / total_revenue <= 0.80 THEN 'A - High Value'
        WHEN cumulative_revenue / total_revenue <= 0.95 THEN 'B - Medium Value'
        ELSE 'C - Low Value'
    END AS abc_class
FROM cumulative
ORDER BY rank;
