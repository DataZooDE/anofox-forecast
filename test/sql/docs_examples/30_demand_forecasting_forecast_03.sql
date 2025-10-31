-- Forecast next 30 days for all products
CREATE TABLE demand_forecast AS
SELECT 
    sku,
    date_col AS forecast_date,
    ROUND(point_forecast, 0) AS forecasted_quantity,
    ROUND(lower, 0) AS min_quantity_95ci,
    ROUND(upper, 0) AS max_quantity_95ci,
    confidence_level
FROM TS_FORECAST_BY('product_sales', sku, sale_date, quantity_sold,
                    'AutoETS', 30,
                    {'seasonal_period': 7, 'confidence_level': 0.95});
