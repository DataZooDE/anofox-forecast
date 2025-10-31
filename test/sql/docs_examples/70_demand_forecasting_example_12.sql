-- Week-ahead forecast aggregated
CREATE VIEW weekly_demand_dashboard AS
WITH next_week AS (
    SELECT 
        sku,
        DATE_TRUNC('week', forecast_date) AS week,
        SUM(forecasted_quantity) AS weekly_demand,
        SUM(max_quantity_95ci) AS weekly_demand_upper
    FROM demand_forecast
    WHERE forecast_date BETWEEN CURRENT_DATE AND CURRENT_DATE + INTERVAL '7 days'
    GROUP BY sku, week
)
SELECT 
    n.sku,
    n.week,
    n.weekly_demand,
    n.weekly_demand_upper,
    i.current_stock,
    v.avg_unit_cost,
    ROUND(n.weekly_demand_upper * v.avg_unit_cost, 2) AS capital_requirement,
    CASE 
        WHEN i.current_stock < n.weekly_demand THEN 'Order required'
        ELSE 'Stock sufficient'
    END AS action
FROM next_week n
JOIN inventory i ON n.sku = i.sku
JOIN vendor_pricing v ON n.sku = v.sku
ORDER BY capital_requirement DESC;
