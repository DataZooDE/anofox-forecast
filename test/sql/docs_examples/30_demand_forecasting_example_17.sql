-- Products at risk of stockout in next 7 days
CREATE VIEW stockout_alerts AS
WITH week_demand AS (
    SELECT 
        sku,
        SUM(max_quantity_95ci) AS week_demand_upper
    FROM demand_forecast
    WHERE forecast_date <= CURRENT_DATE + INTERVAL '7 days'
    GROUP BY sku
)
SELECT 
    w.sku,
    i.current_stock,
    ROUND(w.week_demand_upper, 0) AS week_demand,
    ROUND(i.current_stock - w.week_demand_upper, 0) AS stock_deficit,
    CASE 
        WHEN i.current_stock < w.week_demand_upper * 0.5 THEN '🔴 URGENT'
        WHEN i.current_stock < w.week_demand_upper * 0.75 THEN '🟠 HIGH'
        ELSE '🟡 MEDIUM'
    END AS priority
FROM week_demand w
JOIN inventory i ON w.sku = i.sku
WHERE i.current_stock < w.week_demand_upper
ORDER BY priority, stock_deficit;
