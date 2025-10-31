-- Forecast next 30 days per product
CREATE TABLE product_revenue_forecast AS
SELECT 
    product_id,
    date_col AS forecast_date,
    ROUND(point_forecast, 2) AS revenue_forecast,
    ROUND(lower, 2) AS revenue_lower_90ci,
    ROUND(upper, 2) AS revenue_upper_90ci,
    model_name,
    confidence_level
FROM TS_FORECAST_BY('product_sales_clean', product_id, date, revenue,
                    'AutoETS', 30,
                    {'seasonal_period': 7, 'confidence_level': 0.90});
