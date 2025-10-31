-- Forecast inventory levels to plan warehouse space
WITH inventory_forecast AS (
    -- Forecast incoming (purchases/production)
    SELECT * FROM TS_FORECAST_BY('daily_production', sku, date, units_produced,
                                 'AutoETS', 30, {'seasonal_period': 7})
),
demand_forecast AS (
    -- Forecast outgoing (sales)
    SELECT * FROM TS_FORECAST_BY('daily_sales', sku, date, units_sold,
                                 'AutoETS', 30, {'seasonal_period': 7})
),
net_inventory AS (
    SELECT 
        i.sku,
        i.date_col AS date,
        i.point_forecast AS incoming,
        d.point_forecast AS outgoing,
        i.point_forecast - d.point_forecast AS net_change
    FROM inventory_forecast i
    JOIN demand_forecast d ON i.sku = d.sku AND i.date_col = d.date_col
),
cumulative_inventory AS (
    SELECT 
        sku,
        date,
        net_change,
        SUM(net_change) OVER (PARTITION BY sku ORDER BY date) + 
            (SELECT current_stock FROM inventory WHERE inventory.sku = net_inventory.sku) AS projected_inventory
    FROM net_inventory
),
space_requirements AS (
    SELECT 
        c.date,
        SUM(c.projected_inventory * p.cubic_feet) AS total_cubic_feet_needed
    FROM cumulative_inventory c
    JOIN product_catalog p ON c.sku = p.sku
    GROUP BY c.date
)
SELECT 
    date,
    ROUND(total_cubic_feet_needed / 1000.0, 2) AS thousand_cubic_feet,
    CASE 
        WHEN total_cubic_feet_needed > 50000 THEN '⚠️ Approaching capacity limit'
        ELSE '✓ Within capacity'
    END AS status
FROM space_requirements
WHERE date >= CURRENT_DATE
ORDER BY date;
