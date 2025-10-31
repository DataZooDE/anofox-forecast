-- Allocate limited resources across multiple products
WITH product_forecast AS (
    SELECT * FROM TS_FORECAST_BY('sales', product_id, date, units,
                                 'AutoETS', 30, {'seasonal_period': 7})
),
resource_needs AS (
    SELECT 
        p.product_id,
        SUM(p.point_forecast) AS forecasted_units_30d,
        SUM(p.point_forecast) * pr.labor_hours_per_unit AS labor_hours_needed,
        SUM(p.point_forecast) * pr.machine_hours_per_unit AS machine_hours_needed,
        pc.profit_margin
    FROM product_forecast p
    JOIN production_recipes pr ON p.product_id = pr.product_id
    JOIN product_catalog pc ON p.product_id = pc.product_id
    GROUP BY p.product_id, pr.labor_hours_per_unit, pr.machine_hours_per_unit, pc.profit_margin
),
capacity_limits AS (
    SELECT 720 AS labor_hours_available, 600 AS machine_hours_available
)
SELECT 
    r.product_id,
    ROUND(r.forecasted_units_30d, 0) AS demand_forecast,
    ROUND(r.labor_hours_needed, 1) AS labor_hrs,
    ROUND(r.machine_hours_needed, 1) AS machine_hrs,
    r.profit_margin,
    -- ROI per resource hour
    ROUND(r.profit_margin / (r.labor_hours_needed + r.machine_hours_needed), 2) AS roi_per_hour,
    RANK() OVER (ORDER BY r.profit_margin / (r.labor_hours_needed + r.machine_hours_needed) DESC) AS priority
FROM resource_needs r
CROSS JOIN capacity_limits c
ORDER BY priority;

-- Allocate to highest ROI products first until capacity exhausted
