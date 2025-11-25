-- Identify seasonal products and plan accordingly
WITH seasonality AS (
    SELECT 
        sku,
        TS_DETECT_SEASONALITY(LIST(quantity_sold ORDER BY sale_date)) AS detected_periods
    FROM product_sales
    GROUP BY sku
),
seasonality_with_meta AS (
    SELECT 
        sku,
        detected_periods,
        CASE 
            WHEN LEN(detected_periods) > 0 THEN detected_periods[1]
            ELSE NULL 
        END AS primary_period,
        LEN(detected_periods) > 0 AS is_seasonal
    FROM seasonality
),
forecast_by_season AS (
    SELECT 
        df.sku,
        EXTRACT(MONTH FROM df.forecast_date) AS month,
        AVG(df.forecasted_quantity) AS avg_daily_demand,
        s.primary_period,
        s.is_seasonal
    FROM demand_forecast df
    JOIN seasonality_with_meta s ON df.sku = s.sku
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
