-- Use fast model for most products, accurate for top products
WITH abc_class AS (
    SELECT 
        product_id,
        CASE 
            WHEN revenue_rank <= 100 THEN 'A'
            WHEN revenue_rank <= 500 THEN 'B'
            ELSE 'C'
        END AS class
    FROM (
        SELECT 
            product_id,
            RANK() OVER (ORDER BY SUM(revenue) DESC) AS revenue_rank
        FROM sales
        GROUP BY product_id
    )
),
a_forecasts AS (
    SELECT * FROM TS_FORECAST_BY(
        (SELECT * FROM sales WHERE product_id IN (SELECT product_id FROM abc_class WHERE class = 'A')),
        product_id, date, amount, 'AutoARIMA', 28, {'seasonal_period': 7}
    )
),
b_forecasts AS (
    SELECT * FROM TS_FORECAST_BY(
        (SELECT * FROM sales WHERE product_id IN (SELECT product_id FROM abc_class WHERE class = 'B')),
        product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7}
    )
),
c_forecasts AS (
    SELECT * FROM TS_FORECAST_BY(
        (SELECT * FROM sales WHERE product_id IN (SELECT product_id FROM abc_class WHERE class = 'C')),
        product_id, date, amount, 'SeasonalNaive', 28, {'seasonal_period': 7}
    )
)
SELECT * FROM a_forecasts
UNION ALL SELECT * FROM b_forecasts
UNION ALL SELECT * FROM c_forecasts;
