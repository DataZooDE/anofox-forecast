-- Create sample evaluation data
CREATE TABLE evaluation AS
SELECT 
    product_id,
    100.0 AS actual,
    102.5 AS forecast,
    95.0 AS lower,
    110.0 AS upper
FROM (VALUES (1), (2), (3)) products(product_id);

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

-- Create sample sales data
CREATE TABLE sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS revenue
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Create actuals table
CREATE TABLE actuals AS
SELECT 
    product_id,
    forecast_step AS step,
    100.0 + forecast_step * 2.0 + (RANDOM() * 5) AS actual_value
FROM generate_series(1, 28) t(forecast_step)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Generate forecasts for multiple products
CREATE TEMP TABLE forecasts_temp AS
SELECT * FROM anofox_fcst_ts_forecast_by(
    'sales',
    product_id,
    date,
    revenue,
    'AutoETS',
    30,
    MAP{'seasonal_period': 7}
);

-- Join with actuals and evaluate per product
CREATE TEMP TABLE evaluation AS
SELECT 
    f.product_id,
    a.actual_value AS actual,
    f.point_forecast AS predicted
FROM forecasts_temp f
JOIN actuals a ON f.product_id = a.product_id AND f.forecast_step = a.step;

-- Calculate metrics per product using GROUP BY + LIST()
SELECT 
    product_id,
    anofox_fcst_ts_mae(LIST(actual ORDER BY product_id), LIST(predicted ORDER BY product_id)) AS mae,
    anofox_fcst_ts_rmse(LIST(actual ORDER BY product_id), LIST(predicted ORDER BY product_id)) AS rmse,
    anofox_fcst_ts_mape(LIST(actual ORDER BY product_id), LIST(predicted ORDER BY product_id)) AS mape,
    anofox_fcst_ts_bias(LIST(actual ORDER BY product_id), LIST(predicted ORDER BY product_id)) AS bias
FROM evaluation
GROUP BY product_id
ORDER BY mae;  -- Best forecasts first
