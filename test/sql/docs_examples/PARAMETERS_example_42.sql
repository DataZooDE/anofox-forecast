-- Use specialized intermittent models
SELECT 
    sku,
    warehouse,
    TS_FORECAST(date, demand, 'CrostonSBA', 12, MAP{}) AS forecast
FROM inventory_demand
WHERE demand_type = 'intermittent'
GROUP BY sku, warehouse;
