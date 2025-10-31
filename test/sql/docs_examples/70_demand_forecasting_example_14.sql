-- For new products with limited history, use similar products
WITH similar_products AS (
    SELECT 
        new_sku,
        similar_sku
    FROM product_similarity
    WHERE new_sku = 'NEW_PRODUCT_001'
),
similar_forecast AS (
    SELECT 
        AVG(point_forecast) AS avg_forecast,
        forecast_step
    FROM TS_FORECAST_BY('product_sales', sku, sale_date, quantity_sold,
                        'AutoETS', 30, {'seasonal_period': 7})
    WHERE sku IN (SELECT similar_sku FROM similar_products)
    GROUP BY forecast_step
)
SELECT 
    'NEW_PRODUCT_001' AS sku,
    CURRENT_DATE + forecast_step AS forecast_date,
    ROUND(avg_forecast, 0) AS forecasted_quantity,
    '(Based on similar products)' AS note
FROM similar_forecast;
