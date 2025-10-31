WITH fc AS (
    SELECT 
        product_id,
        location_id,
        TS_FORECAST_AGG(date, amount, 'AutoETS', 28, {'seasonal_period': 7}) AS result
    FROM sales
    GROUP BY product_id, location_id
)
SELECT 
    product_id,
    location_id,
    UNNEST(result.forecast_step) AS forecast_step,
    UNNEST(result.point_forecast) AS point_forecast
FROM fc;
