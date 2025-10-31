-- Project end-of-month revenue based on current pace
WITH mtd_actual AS (
    SELECT SUM(revenue) AS mtd_revenue
    FROM sales
    WHERE date BETWEEN DATE_TRUNC('month', CURRENT_DATE) AND CURRENT_DATE
),
days_passed AS (
    SELECT DATE_DIFF('day', DATE_TRUNC('month', CURRENT_DATE), CURRENT_DATE) AS days
),
days_remaining AS (
    SELECT DATE_DIFF('day', CURRENT_DATE, DATE_TRUNC('month', CURRENT_DATE) + INTERVAL '1 month') AS days
),
remaining_forecast AS (
    SELECT SUM(point_forecast) AS remaining_revenue
    FROM product_revenue_forecast
    WHERE date_col BETWEEN CURRENT_DATE + INTERVAL '1 day' 
                       AND DATE_TRUNC('month', CURRENT_DATE) + INTERVAL '1 month'
)
SELECT 
    ROUND(mtd_revenue, 0) AS month_to_date,
    ROUND(remaining_revenue, 0) AS forecasted_remainder,
    ROUND(mtd_revenue + remaining_revenue, 0) AS projected_month_total,
    days AS days_passed,
    (SELECT days FROM days_remaining) AS days_left
FROM mtd_actual, remaining_forecast, days_passed;
