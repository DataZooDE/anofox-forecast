-- Plan for different scenarios
WITH scenarios AS (
    SELECT 
        'Pessimistic (Lower bound)' AS scenario,
        SUM(lower) AS total_demand
    FROM TS_FORECAST('daily_demand', date, units, 'AutoETS', 30, {'seasonal_period': 7, 'confidence_level': 0.90})
    UNION ALL
    SELECT 
        'Expected',
        SUM(point_forecast)
    FROM TS_FORECAST('daily_demand', date, units, 'AutoETS', 30, {'seasonal_period': 7, 'confidence_level': 0.90})
    UNION ALL
    SELECT 
        'Optimistic (Upper bound)',
        SUM(upper)
    FROM TS_FORECAST('daily_demand', date, units, 'AutoETS', 30, {'seasonal_period': 7, 'confidence_level': 0.90})
)
SELECT 
    scenario,
    ROUND(total_demand, 0) AS forecasted_demand,
    ROUND(total_demand * 1.2, 0) AS recommended_capacity  -- 20% buffer
FROM scenarios;
