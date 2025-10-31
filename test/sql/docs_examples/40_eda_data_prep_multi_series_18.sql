-- Forecast at category level, then disaggregate
WITH category_forecast AS (
    SELECT 
        pc.category,
        date,
        SUM(sales_amount) AS category_sales
    FROM sales s
    JOIN product_catalog pc ON s.product_id = pc.product_id
    GROUP BY pc.category, date
),
category_predictions AS (
    SELECT * FROM TS_FORECAST_BY('category_forecast', category, date, category_sales,
                                 'AutoETS', 28, {'seasonal_period': 7})
),
product_proportions AS (
    SELECT 
        product_id,
        category,
        AVG(sales_amount) / SUM(sales_amount) OVER (PARTITION BY category) AS product_share
    FROM sales s
    JOIN product_catalog pc ON s.product_id = pc.product_id
    GROUP BY product_id, category
)
SELECT 
    pp.product_id,
    cp.date_col AS forecast_date,
    ROUND(cp.point_forecast * pp.product_share, 2) AS product_forecast
FROM category_predictions cp
JOIN product_proportions pp ON cp.category = pp.category;
