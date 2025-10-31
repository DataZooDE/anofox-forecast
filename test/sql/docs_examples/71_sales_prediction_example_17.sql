-- What if sales drop 20%? What if they increase 30%?
WITH base_forecast AS (
    SELECT SUM(point_forecast) AS base_revenue
    FROM product_revenue_forecast
    WHERE date_col <= CURRENT_DATE + INTERVAL '30 days'
),
scenarios AS (
    SELECT 
        'Severe downturn (-30%)' AS scenario,
        base_revenue * 0.70 AS projected_revenue
    FROM base_forecast
    UNION ALL
    SELECT 
        'Moderate downturn (-15%)',
        base_revenue * 0.85
    FROM base_forecast
    UNION ALL
    SELECT 
        'Base case',
        base_revenue
    FROM base_forecast
    UNION ALL
    SELECT 
        'Moderate growth (+15%)',
        base_revenue * 1.15
    FROM base_forecast
    UNION ALL
    SELECT 
        'Strong growth (+30%)',
        base_revenue * 1.30
    FROM base_forecast
)
SELECT 
    scenario,
    ROUND(projected_revenue, 0) AS revenue,
    ROUND(projected_revenue - (SELECT base_revenue FROM base_forecast), 0) AS variance_from_base
FROM scenarios
ORDER BY projected_revenue;
