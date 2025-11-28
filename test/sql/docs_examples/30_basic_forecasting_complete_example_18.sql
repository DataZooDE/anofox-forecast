-- Create sample evaluation data
CREATE TABLE evaluation AS
SELECT 
    product_id,
    100.0 AS actual,
    102.5 AS forecast,
    95.0 AS lower,
    110.0 AS upper
FROM (VALUES (1), (2), (3)) products(product_id);

-- Create sample raw sales data
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- ============================================================================
-- Real-world sales forecasting workflow
-- ============================================================================

-- Step 1: Prepare data
CREATE TEMP TABLE filled AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount, '1d');

CREATE TEMP TABLE cleaned AS
SELECT * FROM TS_DROP_CONSTANT('filled', product_id, sales_amount);

CREATE TABLE sales_prep AS
SELECT 
    product_id,
    date,
    value_col AS sales_amount
FROM TS_FILL_NULLS_FORWARD('cleaned', product_id, date, sales_amount);

-- Step 2: Analyze data
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_prep', product_id, date, sales_amount, '1d');

SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30)
LIMIT 10;

-- Step 3: Detect seasonality
CREATE TABLE seasonality AS
SELECT 
    product_id,
    TS_DETECT_SEASONALITY(LIST(sales_amount ORDER BY date)) AS detected_periods
FROM sales_prep
GROUP BY product_id;

SELECT * FROM seasonality;

-- Step 4: Generate forecasts
CREATE TABLE forecasts AS
SELECT * FROM TS_FORECAST_BY('sales_prep', product_id, date, sales_amount,
                             'AutoETS', 28,
                             MAP{'seasonal_period': 7, 
                              'confidence_level': 0.95,
                              'return_insample': true});

-- Step 5: Review forecasts
SELECT 
    product_id,
    forecast_step,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(lower, 2) AS lower_95,
    ROUND(upper, 2) AS upper_95,
    model_name
FROM forecasts
WHERE forecast_step <= 7
ORDER BY product_id, forecast_step;

-- Step 6: Validate (if you have actuals)
SELECT 
    product_id,
    TS_MAE(LIST(actual ORDER BY product_id), LIST(forecast ORDER BY product_id)) AS mae,
    TS_COVERAGE(LIST(actual ORDER BY product_id), LIST(lower ORDER BY product_id), LIST(upper ORDER BY product_id)) * 100 AS coverage_pct
FROM evaluation
GROUP BY product_id;
