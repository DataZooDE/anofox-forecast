-- ==============================================================================
-- DuckDB Time Series Forecasting Extension - Aggregate Function Demo
-- ==============================================================================
-- This demo shows how to use the TS_FORECAST aggregate function for 
-- batch forecasting with GROUP BY.

LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Create a sample dataset with multiple time series
CREATE OR REPLACE TABLE sales_data AS
SELECT 
    ('2024-01-01'::TIMESTAMP + INTERVAL (i) DAYS) AS date,
    'Product_A' AS product,
    100.0 + i * 2.0 + (random() * 10 - 5) AS sales
FROM range(0, 60) t(i)
UNION ALL
SELECT 
    ('2024-01-01'::TIMESTAMP + INTERVAL (i) DAYS) AS date,
    'Product_B' AS product,
    200.0 + i * 3.0 + (random() * 15 - 7.5) AS sales
FROM range(0, 60) t(i)
UNION ALL
SELECT 
    ('2024-01-01'::TIMESTAMP + INTERVAL (i) DAYS) AS date,
    'Product_C' AS product,
    150.0 + i * 1.5 + (random() * 8 - 4) AS sales
FROM range(0, 60) t(i);

-- Display sample data
SELECT * FROM sales_data 
WHERE date <= '2024-01-10' 
ORDER BY product, date;

-- ==============================================================================
-- Example 1: Aggregate Function - Single Forecast (all data)
-- ==============================================================================
SELECT TS_FORECAST(date, sales, 'Naive', 7, NULL) AS forecast
FROM sales_data;

-- ==============================================================================
-- Example 2: Aggregate Function - Batch Forecasting with GROUP BY
-- ==============================================================================
SELECT 
    product,
    TS_FORECAST(date, sales, 'Naive', 7, NULL) AS forecast
FROM sales_data
GROUP BY product
ORDER BY product;

-- ==============================================================================
-- Example 3: Unnesting the Forecast Results
-- ==============================================================================
-- Extract forecast arrays from the struct and unnest them
SELECT 
    product,
    UNNEST(forecast.forecast_step) AS step,
    UNNEST(forecast.point_forecast) AS forecast_value,
    UNNEST(forecast.lower_95) AS lower_bound,
    UNNEST(forecast.upper_95) AS upper_bound,
    forecast.model_name AS model
FROM (
    SELECT 
        product,
        TS_FORECAST(date, sales, 'Naive', 7, NULL) AS forecast
    FROM sales_data
    GROUP BY product
)
ORDER BY product, step;

-- ==============================================================================
-- Example 4: Different Models with Parameters
-- ==============================================================================
-- Forecast with SMA (Simple Moving Average) - with custom window
SELECT 
    product,
    TS_FORECAST(date, sales, 'SMA', 7, {'window': 10}) AS forecast
FROM sales_data
GROUP BY product
ORDER BY product;

-- Forecast with SeasonalNaive - with seasonal period parameter
SELECT 
    product,
    TS_FORECAST(date, sales, 'SeasonalNaive', 7, {'seasonal_period': 7}) AS forecast
FROM sales_data
GROUP BY product
ORDER BY product;

-- ==============================================================================
-- Example 5: Combining Multiple Models in One Query
-- ==============================================================================
SELECT 
    product,
    'Naive' AS model_type,
    UNNEST(TS_FORECAST(date, sales, 'Naive', 5, NULL).point_forecast) AS forecast
FROM sales_data
GROUP BY product
UNION ALL
SELECT 
    product,
    'SMA' AS model_type,
    UNNEST(TS_FORECAST(date, sales, 'SMA', 5, NULL).point_forecast) AS forecast
FROM sales_data
GROUP BY product
ORDER BY product, model_type;

-- ==============================================================================
-- Example 6: Wide Format - Forecast for Each Product as Columns
-- ==============================================================================
SELECT 
    UNNEST(forecast_a.forecast_step) AS day,
    UNNEST(forecast_a.point_forecast) AS product_a_forecast,
    UNNEST(forecast_b.point_forecast) AS product_b_forecast,
    UNNEST(forecast_c.point_forecast) AS product_c_forecast
FROM (
    SELECT 
        TS_FORECAST(date, sales, 'Naive', 7, NULL) AS forecast_a
    FROM sales_data WHERE product = 'Product_A'
), (
    SELECT 
        TS_FORECAST(date, sales, 'Naive', 7, NULL) AS forecast_b
    FROM sales_data WHERE product = 'Product_B'
), (
    SELECT 
        TS_FORECAST(date, sales, 'Naive', 7, NULL) AS forecast_c
    FROM sales_data WHERE product = 'Product_C'
);

-- ==============================================================================
-- Example 7: Forecast with Confidence Intervals Visualization
-- ==============================================================================
SELECT 
    product,
    UNNEST(forecast.forecast_step) AS horizon,
    ROUND(UNNEST(forecast.point_forecast), 2) AS forecast,
    ROUND(UNNEST(forecast.lower_95), 2) AS ci_lower,
    ROUND(UNNEST(forecast.upper_95), 2) AS ci_upper,
    ROUND(UNNEST(forecast.upper_95) - UNNEST(forecast.lower_95), 2) AS ci_width
FROM (
    SELECT 
        product,
        TS_FORECAST(date, sales, 'Naive', 10, NULL) AS forecast
    FROM sales_data
    GROUP BY product
)
ORDER BY product, horizon;

-- ==============================================================================
-- Example 8: Parameter Tuning - Comparing Different SMA Windows
-- ==============================================================================
SELECT 
    product,
    'window=3' AS config,
    UNNEST(TS_FORECAST(date, sales, 'SMA', 5, {'window': 3}).point_forecast) AS forecast
FROM sales_data
GROUP BY product
UNION ALL
SELECT 
    product,
    'window=7' AS config,
    UNNEST(TS_FORECAST(date, sales, 'SMA', 5, {'window': 7}).point_forecast) AS forecast
FROM sales_data
GROUP BY product
UNION ALL
SELECT 
    product,
    'window=14' AS config,
    UNNEST(TS_FORECAST(date, sales, 'SMA', 5, {'window': 14}).point_forecast) AS forecast
FROM sales_data
GROUP BY product
ORDER BY product, config;

-- ==============================================================================
-- Example 9: Model Performance Metadata
-- ==============================================================================
-- Extract model name and forecast statistics
SELECT 
    product,
    forecast.model_name AS model,
    ROUND(AVG(UNNEST(forecast.point_forecast)), 2) AS avg_forecast,
    ROUND(MIN(UNNEST(forecast.point_forecast)), 2) AS min_forecast,
    ROUND(MAX(UNNEST(forecast.point_forecast)), 2) AS max_forecast,
    ROUND(AVG(UNNEST(forecast.upper_95) - UNNEST(forecast.lower_95)), 2) AS avg_ci_width
FROM (
    SELECT 
        product,
        TS_FORECAST(date, sales, 'Naive', 7, NULL) AS forecast
    FROM sales_data
    GROUP BY product
)
GROUP BY product, forecast.model_name
ORDER BY product;

-- ==============================================================================
-- Cleanup
-- ==============================================================================
-- DROP TABLE sales_data;

