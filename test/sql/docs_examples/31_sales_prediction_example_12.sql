-- Revenue by customer acquisition cohort
WITH customer_cohorts AS (
    SELECT 
        customer_id,
        DATE_TRUNC('month', first_purchase_date) AS cohort_month
    FROM customers
),
cohort_revenue AS (
    SELECT 
        c.cohort_month,
        s.date,
        SUM(s.revenue) AS cohort_revenue
    FROM sales s
    JOIN customer_cohorts c ON s.customer_id = c.customer_id
    GROUP BY c.cohort_month, s.date
),
cohort_forecasts AS (
    SELECT * FROM TS_FORECAST_BY('cohort_revenue', cohort_month, date, cohort_revenue,
                                 'AutoETS', 90, {'seasonal_period': 7})
)
SELECT 
    cohort_month,
    SUM(point_forecast) AS q4_projected_revenue,
    RANK() OVER (ORDER BY SUM(point_forecast) DESC) AS cohort_rank
FROM cohort_forecasts
WHERE date_col BETWEEN '2024-01-01' AND '2024-03-31'
GROUP BY cohort_month
ORDER BY cohort_rank;
