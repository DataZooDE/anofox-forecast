-- Calculate reorder quantities with safety stock
WITH daily_forecast AS (
    SELECT 
        sku,
        forecast_date,
        forecasted_quantity,
        max_quantity_95ci  -- Use upper bound for safety
    FROM demand_forecast
),
weekly_demand AS (
    SELECT 
        sku,
        DATE_TRUNC('week', forecast_date) AS week,
        SUM(forecasted_quantity) AS weekly_forecast,
        SUM(max_quantity_95ci) AS weekly_upper_bound
    FROM daily_forecast
    GROUP BY sku, week
),
inventory_current AS (
    SELECT sku, current_stock, lead_time_days
    FROM inventory
)
SELECT 
    w.sku,
    w.week,
    w.weekly_forecast AS expected_demand,
    w.weekly_upper_bound AS demand_95ci,
    i.current_stock,
    i.lead_time_days,
    GREATEST(0, w.weekly_upper_bound - i.current_stock) AS reorder_quantity,
    CASE 
        WHEN i.current_stock < w.weekly_forecast THEN '🔴 Reorder Now'
        WHEN i.current_stock < w.weekly_upper_bound THEN '🟡 Monitor'
        ELSE '🟢 OK'
    END AS status
FROM weekly_demand w
JOIN inventory_current i ON w.sku = i.sku
WHERE w.week = DATE_TRUNC('week', CURRENT_DATE)
ORDER BY status, reorder_quantity DESC;
