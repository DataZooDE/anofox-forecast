-- Forecast electricity demand for generation planning
WITH hourly_demand AS (
    SELECT * FROM TS_FORECAST('hourly_power_demand', timestamp, megawatts,
                              'AutoTBATS', 168,  -- 1 week
                              {'seasonal_periods': [24, 168]})  -- Daily + weekly
),
generation_capacity AS (
    SELECT 
        DATE_TRUNC('day', date_col) AS date,
        MAX(upper) AS peak_demand_mw,
        AVG(point_forecast) AS avg_demand_mw,
        MIN(lower) AS min_demand_mw
    FROM hourly_demand
    GROUP BY date
),
capacity_plan AS (
    SELECT 
        date,
        peak_demand_mw,
        -- Base load: Coal/Nuclear (slow to adjust)
        min_demand_mw * 0.9 AS baseload_capacity_mw,
        -- Peak load: Gas turbines (quick response)
        peak_demand_mw - min_demand_mw * 0.9 AS peaking_capacity_mw,
        current_baseload,
        current_peaking
    FROM generation_capacity
    CROSS JOIN (SELECT 800 AS current_baseload, 300 AS current_peaking) current
)
SELECT 
    date,
    ROUND(baseload_capacity_mw, 1) AS baseload_needed,
    ROUND(peaking_capacity_mw, 1) AS peaking_needed,
    CASE 
        WHEN baseload_capacity_mw > current_baseload 
        THEN '⚠️ Baseload expansion needed'
        ELSE '✓ Baseload adequate'
    END AS baseload_status,
    CASE 
        WHEN peaking_capacity_mw > current_peaking 
        THEN '⚠️ Add peaking capacity'
        ELSE '✓ Peaking adequate'
    END AS peaking_status
FROM capacity_plan
WHERE date >= CURRENT_DATE
ORDER BY date;
