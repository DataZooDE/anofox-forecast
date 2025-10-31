-- Forecast demand by location and aggregate
WITH location_forecast AS (
    SELECT * FROM TS_FORECAST_BY('sales_by_location', 
                                 location_id || '_' || sku AS series_key,
                                 sale_date, quantity_sold,
                                 'AutoETS', 30, {'seasonal_period': 7})
),
parsed AS (
    SELECT 
        SPLIT_PART(series_key, '_', 1) AS location_id,
        SPLIT_PART(series_key, '_', 2) AS sku,
        forecast_date,
        forecasted_quantity
    FROM location_forecast
)
SELECT 
    sku,
    forecast_date,
    SUM(forecasted_quantity) AS total_demand,
    MAX(forecasted_quantity) AS max_location_demand,
    FIRST(location_id) KEEP (DENSE_RANK FIRST ORDER BY forecasted_quantity DESC) AS top_location
FROM parsed
GROUP BY sku, forecast_date
ORDER BY sku, forecast_date;
