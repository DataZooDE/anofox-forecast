-- Estimate ROI from improved forecasting
WITH current_state AS (
    SELECT 
        SUM(stockout_cost) AS total_stockout_cost,
        SUM(holding_cost) AS total_holding_cost
    FROM inventory_costs_last_year
),
forecast_state AS (
    SELECT 
        f.sku,
        -- Reduce stockouts by (1 - mape/100)
        i.stockout_cost * (a.mape / 100) AS new_stockout_cost,
        -- Reduce holding costs by optimizing inventory
        i.holding_cost * 0.6 AS new_holding_cost  -- Assume 40% reduction
    FROM forecasts f
    JOIN accuracy a ON f.sku = a.sku
    JOIN inventory_costs i ON f.sku = i.sku
),
savings AS (
    SELECT 
        c.total_stockout_cost AS current_stockout,
        c.total_holding_cost AS current_holding,
        SUM(f.new_stockout_cost) AS forecast_stockout,
        SUM(f.new_holding_cost) AS forecast_holding
    FROM current_state c
    CROSS JOIN forecast_state f
    GROUP BY c.total_stockout_cost, c.total_holding_cost
)
SELECT 
    ROUND(current_stockout, 0) AS current_stockout_cost,
    ROUND(forecast_stockout, 0) AS forecast_stockout_cost,
    ROUND(current_stockout - forecast_stockout, 0) AS stockout_savings,
    ROUND(current_holding, 0) AS current_holding_cost,
    ROUND(forecast_holding, 0) AS forecast_holding_cost,
    ROUND(current_holding - forecast_holding, 0) AS holding_savings,
    ROUND((current_stockout - forecast_stockout) + (current_holding - forecast_holding), 0) AS total_annual_savings
FROM savings;
