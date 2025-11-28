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
SELECT * FROM TS_STATS('sales_raw', product_id, date, sales_amount, '1d');

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
-- Step 1: Fill gaps
CREATE TEMP TABLE step1 AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount, '1d');

-- Step 2: Drop constant series
CREATE TEMP TABLE step2 AS
SELECT * FROM TS_DROP_CONSTANT('step1', product_id, sales_amount);

-- Step 3: Drop edge zeros
CREATE TABLE sales_custom AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM TS_DROP_EDGE_ZEROS('step2', product_id, date, sales_amount);

-- ============================================================================
-- Step 4: Validate preparation
-- ============================================================================

-- Re-analyze prepared data
CREATE TABLE sales_prepared AS
SELECT * FROM sales_custom;

CREATE TABLE prepared_stats AS
SELECT * FROM TS_STATS('sales_prepared', product_id, date, sales_amount, '1d');

-- Compare quality scores
SELECT 
    'Before' AS stage,
    ROUND(AVG(CAST(n_null AS DOUBLE)), 4) AS avg_nulls,
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END) AS series_with_nulls
FROM sales_stats
UNION ALL
SELECT 
    'After',
    ROUND(AVG(CAST(n_null AS DOUBLE)), 4),
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END)
FROM prepared_stats
LIMIT 10;

-- ============================================================================
-- Step 5: Forecast on prepared data
-- ============================================================================

SELECT * 
FROM TS_FORECAST_BY(
    'sales_prepared', product_id, date, sales_amount,
    'AutoETS', 28,
    MAP{'seasonal_period': 7}
)
LIMIT 10;
