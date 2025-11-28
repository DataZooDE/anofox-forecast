-- Create sample forecasts data
CREATE TABLE forecasts AS
SELECT 
    product_id,
    forecast_step,
    DATE '2024-01-01' + INTERVAL (forecast_step) DAY AS date,
    100.0 + forecast_step * 2.0 AS point_forecast,
    90.0 + forecast_step * 1.5 AS lower,
    110.0 + forecast_step * 2.5 AS upper
FROM generate_series(1, 28) t(forecast_step)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Create actuals table
CREATE TABLE sales_actuals AS
SELECT 
    product_id,
    DATE '2024-01-01' + INTERVAL (forecast_step) DAY AS date,
    100.0 + forecast_step * 2.0 + (RANDOM() * 5) AS actual_sales
FROM generate_series(1, 28) t(forecast_step)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Assuming you have actual values for the forecast period
WITH actuals AS (
    SELECT product_id, date, actual_sales
    FROM sales_actuals
),
forecasts AS (
    SELECT product_id, date AS date, point_forecast
    FROM forecasts
)
SELECT 
    f.product_id,
    ROUND(TS_MAE(LIST(a.actual_sales ORDER BY a.date), LIST(f.point_forecast ORDER BY f.date)), 2) AS mae,
    ROUND(TS_RMSE(LIST(a.actual_sales ORDER BY a.date), LIST(f.point_forecast ORDER BY f.date)), 2) AS rmse,
    ROUND(TS_MAPE(LIST(a.actual_sales ORDER BY a.date), LIST(f.point_forecast ORDER BY f.date)), 2) AS mape
FROM forecasts f
JOIN actuals a ON f.product_id = a.product_id AND f.date = a.date
GROUP BY f.product_id;
