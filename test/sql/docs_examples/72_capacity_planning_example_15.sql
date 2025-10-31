-- Monitor if actual demand is within capacity
CREATE VIEW capacity_monitor AS
WITH today_forecast AS (
    SELECT SUM(point_forecast) AS expected_today
    FROM TS_FORECAST('daily_demand', date, units, 'AutoETS', 1, {'seasonal_period': 7})
),
today_actual AS (
    SELECT SUM(units) AS actual_so_far
    FROM sales
    WHERE date = CURRENT_DATE
),
today_capacity AS (
    SELECT available_capacity
    FROM capacity_plan
    WHERE date = CURRENT_DATE
)
SELECT 
    a.actual_so_far,
    f.expected_today,
    c.available_capacity,
    ROUND(100.0 * a.actual_so_far / c.available_capacity, 1) AS capacity_utilized_pct,
    ROUND(100.0 * a.actual_so_far / f.expected_today, 1) AS vs_forecast_pct,
    CASE 
        WHEN a.actual_so_far > c.available_capacity * 0.95 THEN '🔴 Near capacity limit'
        WHEN a.actual_so_far > c.available_capacity * 0.85 THEN '🟠 High utilization'
        WHEN a.actual_so_far > c.available_capacity * 0.70 THEN '🟡 Moderate'
        ELSE '🟢 Low utilization'
    END AS status
FROM today_actual a, today_forecast f, today_capacity c;
