-- Create multi-product data
CREATE TABLE product_sales AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Forecast all products at once
SELECT 
    product_id,
    forecast_step,
    ROUND(point_forecast, 2) AS forecast
FROM TS_FORECAST_BY(
    'product_sales',
    product_id,      -- GROUP BY this column
    date,
    sales,
    'AutoETS',
    14,
    MAP{'seasonal_period': 7}
)
WHERE forecast_step <= 3
ORDER BY product_id, forecast_step;
