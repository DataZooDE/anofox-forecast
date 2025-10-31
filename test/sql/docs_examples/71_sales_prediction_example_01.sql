-- Load extension
LOAD 'anofox_forecast.duckdb_extension';

-- Forecast next quarter's revenue
WITH daily_forecast AS (
    SELECT * FROM TS_FORECAST('daily_revenue', date, revenue, 'AutoETS', 90,
                              {'seasonal_period': 7, 'confidence_level': 0.90})
),
quarterly_projection AS (
    SELECT 
        SUM(point_forecast) AS projected_revenue,
        SUM(lower) AS conservative_revenue,
        SUM(upper) AS optimistic_revenue
    FROM daily_forecast
)
SELECT 
    ROUND(projected_revenue, 0) AS q4_projection,
    ROUND(conservative_revenue, 0) AS worst_case_90ci,
    ROUND(optimistic_revenue, 0) AS best_case_90ci,
    ROUND(optimistic_revenue - conservative_revenue, 0) AS uncertainty_range
FROM quarterly_projection;
