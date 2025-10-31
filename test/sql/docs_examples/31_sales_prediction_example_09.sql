-- Three scenarios for board presentation
WITH scenarios AS (
    SELECT 
        'Most Likely (Expected)' AS scenario,
        SUM(point_forecast) AS revenue
    FROM product_revenue_forecast
    UNION ALL
    SELECT 
        'Pessimistic (Lower 90% CI)',
        SUM(lower)
    FROM product_revenue_forecast
    UNION ALL
    SELECT 
        'Optimistic (Upper 90% CI)',
        SUM(upper)
    FROM product_revenue_forecast
),
variance AS (
    SELECT 
        scenario,
        ROUND(revenue, 0) AS projected_revenue,
        ROUND(100.0 * revenue / (SELECT revenue FROM scenarios WHERE scenario LIKE 'Most%') - 100, 1) AS variance_pct
    FROM scenarios
)
SELECT * FROM variance
ORDER BY projected_revenue DESC;
