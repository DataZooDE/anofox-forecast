-- Calculate revenue at risk from forecast uncertainty
WITH uncertainty AS (
    SELECT 
        product_id,
        SUM(point_forecast) AS expected_revenue,
        SUM(lower) AS worst_case_revenue,
        SUM(upper) AS best_case_revenue
    FROM product_revenue_forecast
    WHERE forecast_date <= CURRENT_DATE + INTERVAL '30 days'
    GROUP BY product_id
)
SELECT 
    product_id,
    ROUND(expected_revenue, 0) AS expected,
    ROUND(expected_revenue - worst_case_revenue, 0) AS downside_risk,
    ROUND(best_case_revenue - expected_revenue, 0) AS upside_potential,
    ROUND(100.0 * (worst_case_revenue / expected_revenue - 1), 1) || '%' AS downside_pct,
    ROUND(100.0 * (best_case_revenue / expected_revenue - 1), 1) || '%' AS upside_pct
FROM uncertainty
ORDER BY downside_risk DESC;
