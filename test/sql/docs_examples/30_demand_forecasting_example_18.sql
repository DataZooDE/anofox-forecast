-- Products with excess stock (> 60 days supply)
CREATE VIEW overstock_alerts AS
WITH monthly_forecast AS (
    SELECT 
        sku,
        AVG(forecasted_quantity) AS avg_daily_demand
    FROM demand_forecast
    WHERE forecast_date <= CURRENT_DATE + INTERVAL '30 days'
    GROUP BY sku
)
SELECT 
    i.sku,
    i.current_stock,
    ROUND(f.avg_daily_demand, 2) AS avg_daily_demand,
    ROUND(i.current_stock / NULLIF(f.avg_daily_demand, 0), 0) AS days_of_supply,
    ROUND(i.current_stock * p.unit_cost, 2) AS capital_tied_up,
    CASE 
        WHEN i.current_stock / f.avg_daily_demand > 90 THEN '🔴 Critical overstock'
        WHEN i.current_stock / f.avg_daily_demand > 60 THEN '🟠 High overstock'
        ELSE '🟡 Consider clearance'
    END AS action
FROM inventory i
JOIN monthly_forecast f ON i.sku = f.sku
JOIN product_catalog p ON i.sku = p.sku
WHERE i.current_stock / NULLIF(f.avg_daily_demand, 0) > 60
ORDER BY days_of_supply DESC;
