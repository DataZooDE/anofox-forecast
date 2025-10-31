-- Instead of keeping all forecast steps
CREATE TABLE forecasts_summary AS
SELECT 
    product_id,
    SUM(point_forecast) AS total_forecast,
    AVG(point_forecast) AS avg_forecast,
    MAX(point_forecast) AS peak_forecast
FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28, {'seasonal_period': 7})
GROUP BY product_id;

-- Don't store all intermediate forecast steps if not needed
