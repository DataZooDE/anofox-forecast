-- Track utilization vs targets
WITH capacity_utilization AS (
    SELECT 
        DATE_TRUNC('week', date_col) AS week,
        AVG(point_forecast) / available_capacity AS avg_utilization
    FROM TS_FORECAST('daily_demand', date, units, 'AutoETS', 30, {'seasonal_period': 7})
    CROSS JOIN (SELECT 1000 AS available_capacity) cap
    GROUP BY week
)
SELECT 
    week,
    ROUND(avg_utilization * 100, 1) AS utilization_pct,
    CASE 
        WHEN avg_utilization BETWEEN 0.75 AND 0.85 THEN '🌟 Optimal (75-85%)'
        WHEN avg_utilization BETWEEN 0.65 AND 0.95 THEN '✅ Good'
        WHEN avg_utilization < 0.65 THEN '⚠️ Under-utilized'
        ELSE '🔴 Over-utilized - expand capacity'
    END AS assessment
FROM capacity_utilization
ORDER BY week;
