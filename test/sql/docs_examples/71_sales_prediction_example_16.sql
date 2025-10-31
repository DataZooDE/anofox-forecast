-- 90% confidence: 10% chance revenue will be below this
WITH revenue_dist AS (
    SELECT 
        DATE_TRUNC('quarter', date_col) AS quarter,
        SUM(lower) AS var_10pct,  -- 10th percentile (for 90% CI lower bound)
        SUM(point_forecast) AS expected,
        SUM(upper) AS var_90pct   -- 90th percentile
    FROM product_revenue_forecast
    GROUP BY quarter
)
SELECT 
    quarter,
    ROUND(expected, 0) AS expected_revenue,
    ROUND(var_10pct, 0) AS revenue_at_risk_10pct,
    ROUND(expected - var_10pct, 0) AS potential_shortfall,
    ROUND(100.0 * (expected - var_10pct) / expected, 1) || '%' AS shortfall_pct
FROM revenue_dist
ORDER BY quarter;
