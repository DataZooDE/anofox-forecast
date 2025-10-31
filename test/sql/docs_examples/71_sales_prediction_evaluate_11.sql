-- Monthly revenue projection dashboard
CREATE VIEW executive_revenue_dashboard AS
WITH monthly_forecast AS (
    SELECT 
        DATE_TRUNC('month', date_col) AS month,
        SUM(point_forecast) AS forecasted_revenue,
        SUM(lower) AS conservative_revenue,
        SUM(upper) AS optimistic_revenue
    FROM product_revenue_forecast
    GROUP BY month
),
historical_monthly AS (
    SELECT 
        DATE_TRUNC('month', date) AS month,
        SUM(revenue) AS actual_revenue
    FROM product_sales_clean
    GROUP BY month
),
combined AS (
    SELECT 
        COALESCE(f.month, h.month) AS month,
        h.actual_revenue,
        f.forecasted_revenue,
        f.conservative_revenue,
        f.optimistic_revenue,
        CASE WHEN h.actual_revenue IS NOT NULL THEN 'Actual' ELSE 'Forecast' END AS data_type
    FROM monthly_forecast f
    FULL OUTER JOIN historical_monthly h ON f.month = h.month
)
SELECT 
    month,
    data_type,
    ROUND(COALESCE(actual_revenue, forecasted_revenue), 0) AS revenue,
    ROUND(conservative_revenue, 0) AS lower_bound,
    ROUND(optimistic_revenue, 0) AS upper_bound,
    ROUND(100.0 * (revenue - LAG(revenue) OVER (ORDER BY month)) / LAG(revenue) OVER (ORDER BY month), 1) AS mom_growth_pct
FROM combined
ORDER BY month;
