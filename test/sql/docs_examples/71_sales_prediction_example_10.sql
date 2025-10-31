-- How sensitive is revenue to forecast accuracy?
WITH accuracy_scenarios AS (
    SELECT 
        '100% accuracy' AS scenario,
        SUM(point_forecast) AS revenue,
        1.00 AS accuracy_factor
    FROM product_revenue_forecast
    UNION ALL
    SELECT 
        '95% accuracy',
        SUM(point_forecast) * 0.95,
        0.95
    FROM product_revenue_forecast
    UNION ALL
    SELECT 
        '90% accuracy',
        SUM(point_forecast) * 0.90,
        0.90
    FROM product_revenue_forecast
    UNION ALL
    SELECT 
        '85% accuracy',
        SUM(point_forecast) * 0.85,
        0.85
    FROM product_revenue_forecast
)
SELECT 
    scenario,
    ROUND(revenue, 0) AS projected_revenue,
    ROUND(revenue - LAG(revenue) OVER (ORDER BY accuracy_factor DESC), 0) AS revenue_loss
FROM accuracy_scenarios
ORDER BY accuracy_factor DESC;
