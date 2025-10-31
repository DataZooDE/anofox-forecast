-- Forecast equipment hours needed
WITH equipment_demand AS (
    SELECT 
        DATE_TRUNC('week', p.date_col) AS week,
        e.equipment_type,
        SUM(p.point_forecast * pr.equipment_hours_per_unit) AS hours_needed
    FROM product_demand p
    JOIN production_recipes pr ON p.sku = pr.sku
    JOIN equipment e ON pr.equipment_id = e.equipment_id
    GROUP BY week, e.equipment_type
),
equipment_capacity AS (
    SELECT 
        equipment_type,
        COUNT(*) * 168 * 0.85 AS weekly_capacity_hours  -- 85% OEE
    FROM equipment
    WHERE status = 'Active'
    GROUP BY equipment_type
)
SELECT 
    d.week,
    d.equipment_type,
    ROUND(d.hours_needed, 1) AS hours_needed,
    c.weekly_capacity_hours,
    ROUND(100.0 * d.hours_needed / c.weekly_capacity_hours, 1) AS utilization_pct,
    CASE 
        WHEN d.hours_needed > c.weekly_capacity_hours THEN 
            CEIL((d.hours_needed - c.weekly_capacity_hours) / (168 * 0.85)) || ' additional units needed'
        ELSE 'Capacity sufficient'
    END AS recommendation
FROM equipment_demand d
JOIN equipment_capacity c ON d.equipment_type = c.equipment_type
ORDER BY d.week, utilization_pct DESC;
