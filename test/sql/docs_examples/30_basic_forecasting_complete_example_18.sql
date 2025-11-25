-- ============================================================================
-- Real-world sales forecasting workflow
-- ============================================================================

-- Step 1: Prepare data
CREATE TABLE sales_prep AS
WITH filled AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
),
cleaned AS (
    SELECT * FROM TS_DROP_CONSTANT('filled', product_id, sales_amount)
),
complete AS (
    SELECT * FROM TS_FILL_NULLS_FORWARD('cleaned', product_id, date, sales_amount)
)
SELECT * FROM complete;

-- Step 2: Analyze data
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_prep', product_id, date, sales_amount);

SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30);

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
                             {'seasonal_period': 7, 
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
    TS_MAE(LIST(actual), LIST(forecast)) AS mae,
    TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) * 100 AS coverage_pct
FROM evaluation
GROUP BY product_id;
