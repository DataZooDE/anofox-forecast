-- Forecast by region for territory planning
WITH regional_sales AS (
    SELECT 
        r.region,
        s.date,
        SUM(s.revenue) AS regional_revenue
    FROM sales s
    JOIN store_locations sl ON s.store_id = sl.store_id
    JOIN regions r ON sl.region_id = r.region_id
    GROUP BY r.region, s.date
),
forecasts AS (
    SELECT * FROM TS_FORECAST_BY('regional_sales', region, date, regional_revenue,
                                 'AutoETS', 90,
                                 {'seasonal_period': 7, 'confidence_level': 0.95})
)
SELECT 
    region,
    DATE_TRUNC('month', date_col) AS month,
    SUM(point_forecast) AS monthly_revenue_forecast,
    SUM(upper) AS optimistic_scenario,
    SUM(lower) AS conservative_scenario
FROM forecasts
GROUP BY region, month
ORDER BY region, month;
