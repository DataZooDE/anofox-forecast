-- Forecast production requirements
WITH product_demand AS (
    SELECT * FROM TS_FORECAST_BY('daily_orders', sku, order_date, quantity_ordered,
                                 'AutoETS', 60, {'seasonal_period': 7})
),
production_hours AS (
    SELECT 
        p.sku,
        p.date_col,
        p.point_forecast * pc.hours_per_unit AS hours_needed,
        p.upper * pc.hours_per_unit AS hours_95ci
    FROM product_demand p
    JOIN product_catalog pc ON p.sku = pc.sku
),
daily_capacity AS (
    SELECT 
        date_col AS date,
        SUM(hours_needed) AS total_hours_needed,
        SUM(hours_95ci) AS total_hours_95ci
    FROM production_hours
    GROUP BY date_col
),
capacity_gaps AS (
    SELECT 
        d.date,
        d.total_hours_needed,
        d.total_hours_95ci,
        f.available_capacity_hours,
        d.total_hours_95ci - f.available_capacity_hours AS capacity_gap,
        CASE 
            WHEN d.total_hours_95ci <= f.available_capacity_hours THEN '🟢 Sufficient'
            WHEN d.total_hours_needed <= f.available_capacity_hours THEN '🟡 Tight'
            ELSE '🔴 Insufficient - need overtime or delay'
        END AS status
    FROM daily_capacity d
    JOIN factory_capacity f ON d.date = f.date
)
SELECT * FROM capacity_gaps
WHERE date >= CURRENT_DATE
ORDER BY capacity_gap DESC;
