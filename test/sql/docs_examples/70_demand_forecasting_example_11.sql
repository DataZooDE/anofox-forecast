-- Today's demand forecast for all products
CREATE VIEW daily_demand_dashboard AS
SELECT 
    df.sku,
    pc.product_name,
    pc.category,
    df.forecasted_quantity AS tomorrow_forecast,
    df.min_quantity_95ci AS conservative_estimate,
    df.max_quantity_95ci AS optimistic_estimate,
    i.current_stock,
    CASE 
        WHEN i.current_stock >= df.max_quantity_95ci THEN '🟢 Sufficient'
        WHEN i.current_stock >= df.forecasted_quantity THEN '🟡 Adequate'
        WHEN i.current_stock >= df.min_quantity_95ci THEN '🟠 Low'
        ELSE '🔴 Critical - Reorder NOW'
    END AS stock_status,
    GREATEST(0, df.max_quantity_95ci - i.current_stock) AS suggested_reorder
FROM demand_forecast df
JOIN product_catalog pc ON df.sku = pc.sku
JOIN inventory i ON df.sku = i.sku
WHERE df.forecast_date = CURRENT_DATE + INTERVAL '1 day'
ORDER BY 
    CASE stock_status
        WHEN '🔴 Critical - Reorder NOW' THEN 1
        WHEN '🟠 Low' THEN 2
        WHEN '🟡 Adequate' THEN 3
        ELSE 4
    END,
    suggested_reorder DESC;
