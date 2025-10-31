-- Forecast demand during promotion period
WITH base_forecast AS (
    SELECT * FROM TS_FORECAST_BY('product_sales', sku, sale_date, quantity_sold,
                                 'AutoETS', 30, {'seasonal_period': 7})
),
promotion_lift AS (
    -- Historical promotion impact
    SELECT 
        sku,
        AVG(promotion_lift_factor) AS avg_lift
    FROM historical_promotions
    GROUP BY sku
)
SELECT 
    bf.sku,
    bf.forecast_date,
    ROUND(bf.forecasted_quantity, 0) AS base_demand,
    ROUND(bf.forecasted_quantity * (1 + pl.avg_lift), 0) AS promo_demand,
    ROUND(bf.forecasted_quantity * pl.avg_lift, 0) AS incremental_demand
FROM base_forecast bf
JOIN promotion_lift pl ON bf.sku = pl.sku
WHERE bf.forecast_date BETWEEN '2024-03-01' AND '2024-03-07'  -- Promotion week
ORDER BY bf.sku, bf.forecast_date;
