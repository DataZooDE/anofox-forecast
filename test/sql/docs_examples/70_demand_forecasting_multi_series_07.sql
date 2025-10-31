-- Calculate optimal safety stock levels
WITH forecast_variance AS (
    SELECT 
        sku,
        AVG(forecasted_quantity) AS avg_demand,
        AVG(upper - lower) AS avg_uncertainty,
        STDDEV(forecasted_quantity) AS demand_volatility
    FROM demand_forecast
    GROUP BY sku
),
service_level AS (
    -- Service level factor (95% → z=1.645, 99% → z=2.326)
    SELECT 1.645 AS z_score  -- 95% service level
)
SELECT 
    f.sku,
    ROUND(f.avg_demand, 2) AS average_demand,
    ROUND(f.demand_volatility, 2) AS volatility,
    ROUND(s.z_score * f.demand_volatility * SQRT(i.lead_time_days), 0) AS safety_stock,
    ROUND(f.avg_demand * i.lead_time_days, 0) AS cycle_stock,
    ROUND(f.avg_demand * i.lead_time_days + s.z_score * f.demand_volatility * SQRT(i.lead_time_days), 0) AS reorder_point
FROM forecast_variance f
CROSS JOIN service_level s
JOIN inventory i ON f.sku = i.sku
ORDER BY safety_stock DESC;
