-- Simple ASCII visualization
WITH fc AS (
    SELECT 
        forecast_step,
        point_forecast,
        REPEAT('█', CAST(point_forecast / 5 AS INT)) AS bar
    FROM TS_FORECAST('my_sales', date, sales, 'AutoETS', 14, {'seasonal_period': 7})
)
SELECT forecast_step, ROUND(point_forecast, 1) AS forecast, bar
FROM fc;
