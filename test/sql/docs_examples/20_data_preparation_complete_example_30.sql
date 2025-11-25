-- Load macros
.read sql/eda_time_series.sql
.read sql/data_preparation.sql

-- ============================================================================
-- Step 1: Load and explore data
-- ============================================================================

CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL  -- 5% missing
        WHEN RANDOM() < 0.10 THEN 0.0   -- 5% zeros
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 364) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- ============================================================================
-- Step 2: EDA - Analyze data quality
-- ============================================================================

-- Generate statistics
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_raw', product_id, date, sales_amount);

-- View overall summary
SELECT * FROM TS_STATS_SUMMARY('sales_stats');

-- Generate quality report
SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30);

-- Detect seasonality
CREATE TABLE sales_seasonality AS
SELECT 
    product_id,
    TS_DETECT_SEASONALITY(LIST(sales_amount ORDER BY date)) AS detected_periods
FROM sales_raw
GROUP BY product_id;

SELECT * FROM sales_seasonality;

-- ============================================================================
-- Step 3: Data Preparation
-- ============================================================================

-- Custom pipeline using individual macros
CREATE TABLE sales_custom AS
WITH 
step1 AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
),
step2 AS (
    SELECT * FROM TS_DROP_CONSTANT('step1', product_id, sales_amount)
),
step3 AS (
    SELECT * FROM TS_DROP_EDGE_ZEROS('step2', product_id, date, sales_amount)
),
step4 AS (
    SELECT * FROM TS_FILL_NULLS_INTERPOLATE('step3', product_id, date, sales_amount)
)
SELECT * FROM step4;

-- ============================================================================
-- Step 4: Validate preparation
-- ============================================================================

-- Re-analyze prepared data
CREATE TABLE prepared_stats AS
SELECT * FROM TS_STATS('sales_prepared', product_id, date, sales_amount);

-- Compare quality scores
SELECT 
    'Before' AS stage,
    ROUND(AVG(quality_score), 4) AS avg_quality,
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END) AS series_with_nulls
FROM sales_stats
UNION ALL
SELECT 
    'After',
    ROUND(AVG(quality_score), 4),
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END)
FROM prepared_stats;

-- ============================================================================
-- Step 5: Forecast on prepared data
-- ============================================================================

SELECT * 
FROM TS_FORECAST_BY(
    'sales_prepared', product_id, date, sales_amount,
    'AutoETS', 28,
    {'seasonal_period': 7}
)
LIMIT 10;
