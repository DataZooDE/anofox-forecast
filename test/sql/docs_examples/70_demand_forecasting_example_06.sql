-- Identify seasonal products and plan accordingly
WITH seasonality AS (
    SELECT * FROM TS_DETECT_SEASONALITY_ALL('product_sales', sku, sale_date, quantity_sold)
),
forecast_by_season AS (
    SELECT 
        df.sku,
        EXTRACT(MONTH FROM df.forecast_date) AS month,
        AVG(df.forecasted_quantity) AS avg_daily_demand,
        s.primary_period,
        s.is_seasonal
    FROM demand_forecast df
    JOIN seasonality s ON df.sku = s.series_id
    GROUP BY df.sku, month, s.primary_period, s.is_seasonal
)
SELECT 
    sku,
    month,
    ROUND(avg_daily_demand, 1) AS avg_demand,
    ROUND(avg_daily_demand * 30, 0) AS monthly_demand,
    CASE 
        WHEN is_seasonal THEN 'Adjust stock by season'
        ELSE 'Maintain steady stock'
    END AS inventory_strategy
FROM forecast_by_season
ORDER BY sku, month;
