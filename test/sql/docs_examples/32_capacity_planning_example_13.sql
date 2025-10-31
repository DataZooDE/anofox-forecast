-- What if demand grows 20%? 50%?
WITH base_forecast AS (
    SELECT * FROM TS_FORECAST('daily_demand', date, units, 'AutoETS', 90, {'seasonal_period': 7})
),
growth_scenarios AS (
    SELECT 
        'Current trajectory' AS scenario,
        SUM(point_forecast) AS total_demand,
        1.0 AS growth_factor
    FROM base_forecast
    UNION ALL
    SELECT 
        'Moderate growth (+20%)',
        SUM(point_forecast) * 1.20,
        1.20
    FROM base_forecast
    UNION ALL
    SELECT 
        'Strong growth (+50%)',
        SUM(point_forecast) * 1.50,
        1.50
    FROM base_forecast
    UNION ALL
    SELECT 
        'Explosive growth (+100%)',
        SUM(point_forecast) * 2.00,
        2.00
    FROM base_forecast
)
SELECT 
    scenario,
    ROUND(total_demand, 0) AS quarterly_demand,
    ROUND(total_demand / 90.0, 0) AS avg_daily_demand,
    CEIL(total_demand / 90.0 / 100.0) AS resources_needed  -- Each resource handles 100 units/day
FROM growth_scenarios
ORDER BY growth_factor;
