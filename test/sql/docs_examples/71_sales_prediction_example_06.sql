-- For new products: use similar product patterns
WITH new_products AS (
    SELECT product_id, category, launch_date
    FROM product_catalog
    WHERE launch_date > CURRENT_DATE - INTERVAL '30 days'
),
similar_product_curves AS (
    SELECT 
        pc.category,
        DATEDIFF('day', pc.launch_date, s.date) AS days_since_launch,
        AVG(s.revenue) AS avg_revenue_day_n
    FROM sales s
    JOIN product_catalog pc ON s.product_id = pc.product_id
    WHERE pc.launch_date IS NOT NULL
      AND DATEDIFF('day', pc.launch_date, s.date) BETWEEN 0 AND 90
    GROUP BY pc.category, days_since_launch
),
new_product_projection AS (
    SELECT 
        np.product_id,
        np.launch_date + INTERVAL (spc.days_since_launch) DAY AS projected_date,
        spc.avg_revenue_day_n AS projected_revenue
    FROM new_products np
    JOIN similar_product_curves spc ON np.category = spc.category
    WHERE spc.days_since_launch <= 90
)
SELECT 
    product_id,
    DATE_TRUNC('month', projected_date) AS month,
    SUM(projected_revenue) AS monthly_projection
FROM new_product_projection
GROUP BY product_id, month
ORDER BY product_id, month;
