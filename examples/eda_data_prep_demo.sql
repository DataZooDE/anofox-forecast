-- ============================================================================
-- EDA & Data Preparation Demo
-- ============================================================================
-- Comprehensive demonstration of time series EDA and data preparation
-- using DuckDB SQL

-- Load extension and macros
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
.read sql/eda_time_series.sql
.read sql/data_preparation.sql

SELECT '=== EDA & Data Preparation Demo ===' AS section;

-- ============================================================================
-- PART 1: Create Realistic Sample Data with Quality Issues
-- ============================================================================

SELECT '=== Part 1: Generate Sample Data ===' AS section;

-- Create multi-series sales data with various quality issues
CREATE OR REPLACE TABLE sales_raw AS
WITH products AS (
    SELECT * FROM (VALUES 
        ('P001', 'Electronics'),
        ('P002', 'Clothing'),
        ('P003', 'Food'),
        ('P004', 'Books'),
        ('P005', 'Toys')
    ) t(product_id, category)
),
dates AS (
    SELECT DATE '2022-01-01' + INTERVAL (d) DAY AS date
    FROM generate_series(0, 729) t(d)  -- 2 years
),
base_sales AS (
    SELECT 
        p.product_id,
        p.category,
        d.date,
        -- Seasonal pattern with trend
        100 * (1 + 0.001 * EPOCH(d.date) / 86400) +  -- Trend
        50 * SIN(2 * PI() * EPOCH(d.date) / (86400 * 7)) +  -- Weekly
        30 * SIN(2 * PI() * EPOCH(d.date) / (86400 * 365.25)) +  -- Yearly
        (RANDOM() * 40 - 20) AS base_amount  -- Noise
    FROM products p
    CROSS JOIN dates d
)
SELECT 
    product_id,
    category,
    date,
    CASE 
        -- Introduce various quality issues
        WHEN RANDOM() < 0.03 THEN NULL  -- 3% missing values
        WHEN product_id = 'P001' AND date < DATE '2022-02-15' THEN 0  -- Leading zeros
        WHEN product_id = 'P002' AND date > DATE '2023-11-15' THEN 0  -- Trailing zeros
        WHEN product_id = 'P003' THEN 42.0  -- Constant series
        WHEN product_id = 'P004' AND RANDOM() < 0.15 THEN 0  -- Intermittent (15% zeros)
        WHEN RANDOM() < 0.01 THEN base_amount * 5  -- 1% outliers
        ELSE ROUND(base_amount, 2)
    END AS sales_amount
FROM base_sales;

-- Randomly remove some dates to create gaps
DELETE FROM sales_raw 
WHERE RANDOM() < 0.05  -- Remove 5% of dates
AND product_id NOT IN ('P003');  -- Keep P003 intact for constant series demo

SELECT 'Created ' || COUNT(*) || ' rows for ' || COUNT(DISTINCT product_id) || ' products' AS info
FROM sales_raw;

-- ============================================================================
-- PART 2: EDA - Exploratory Data Analysis
-- ============================================================================

SELECT '=== Part 2: EDA ===' AS section;

-- 2a. Generate comprehensive statistics
SELECT '2a. Per-series statistics' AS example;
CREATE OR REPLACE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_raw', product_id, date, sales_amount);

SELECT 
    product_id,
    length,
    n_gaps,
    n_null,
    n_zeros,
    is_constant,
    ROUND(quality_score, 4) AS quality_score
FROM sales_stats
ORDER BY quality_score;

-- 2b. Dataset summary
SELECT '2b. Dataset summary' AS example;
SELECT * FROM TS_DATASET_SUMMARY('sales_stats');

-- 2c. Comprehensive quality report
SELECT '2c. Quality report' AS example;
SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30);

-- 2d. Analyze edge zeros
SELECT '2d. Edge zeros analysis' AS example;
SELECT * FROM TS_ANALYZE_ZEROS('sales_raw', product_id, date, sales_amount)
ORDER BY total_edge_zeros DESC;

-- 2e. Plateau detection
SELECT '2e. Plateau detection' AS example;
SELECT * FROM TS_DETECT_PLATEAUS('sales_raw', product_id, date, sales_amount)
WHERE max_plateau_size > 5
ORDER BY max_plateau_size DESC;

-- 2f. Seasonality detection
SELECT '2f. Seasonality detection' AS example;
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales_raw', product_id, date, sales_amount);

-- 2g. Distribution percentiles
SELECT '2g. Value distribution' AS example;
SELECT product_id, p25, p50, p75, iqr
FROM TS_PERCENTILES('sales_raw', product_id, sales_amount)
ORDER BY product_id;

-- 2h. Identify problematic series
SELECT '2h. Problematic series (quality < 0.7)' AS example;
SELECT * FROM TS_GET_PROBLEMATIC('sales_stats', 0.7);

-- ============================================================================
-- PART 3: Data Preparation - Individual Operations
-- ============================================================================

SELECT '=== Part 3: Data Preparation ===' AS section;

-- 3a. Fill time gaps
SELECT '3a. Fill time gaps' AS example;
CREATE OR REPLACE TABLE sales_filled AS
SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount);

SELECT 
    'Before fill_gaps' AS stage,
    COUNT(*) AS total_rows,
    COUNT(DISTINCT product_id) AS num_series
FROM sales_raw
UNION ALL
SELECT 
    'After fill_gaps',
    COUNT(*),
    COUNT(DISTINCT product_id)
FROM sales_filled;

-- 3b. Drop constant series
SELECT '3b. Drop constant series' AS example;
CREATE OR REPLACE TABLE sales_variable AS
SELECT * FROM TS_DROP_CONSTANT('sales_filled', product_id, sales_amount);

SELECT 
    'Before drop_constant' AS stage,
    COUNT(DISTINCT product_id) AS num_series
FROM sales_filled
UNION ALL
SELECT 
    'After drop_constant',
    COUNT(DISTINCT product_id)
FROM sales_variable;

-- 3c. Drop short series (if any < 365 days)
SELECT '3c. Drop short series (< 365 obs)' AS example;
CREATE OR REPLACE TABLE sales_long AS
SELECT * FROM TS_DROP_SHORT('sales_variable', product_id, date, 365);

SELECT 
    'Before drop_short' AS stage,
    COUNT(DISTINCT product_id) AS num_series,
    MIN(cnt) AS min_length,
    MAX(cnt) AS max_length
FROM (
    SELECT product_id, COUNT(*) AS cnt
    FROM sales_variable
    GROUP BY product_id
)
UNION ALL
SELECT 
    'After drop_short',
    COUNT(DISTINCT product_id),
    MIN(cnt),
    MAX(cnt)
FROM (
    SELECT product_id, COUNT(*) AS cnt
    FROM sales_long
    GROUP BY product_id
);

-- 3d. Remove edge zeros
SELECT '3d. Remove edge zeros' AS example;
CREATE OR REPLACE TABLE sales_no_edges AS
SELECT * FROM TS_DROP_EDGE_ZEROS('sales_long', product_id, date, sales_amount);

-- Compare lengths
WITH before AS (
    SELECT product_id, COUNT(*) AS before_count
    FROM sales_long
    GROUP BY product_id
),
after AS (
    SELECT product_id, COUNT(*) AS after_count
    FROM sales_no_edges
    GROUP BY product_id
)
SELECT 
    b.product_id,
    b.before_count,
    a.after_count,
    b.before_count - a.after_count AS rows_removed
FROM before b
JOIN after a ON b.product_id = a.product_id
WHERE b.before_count != a.after_count;

-- 3e. Fill missing values - compare methods
SELECT '3e. Fill missing values - method comparison' AS example;

-- Forward fill
CREATE OR REPLACE TABLE sales_forward AS
SELECT * FROM TS_FILL_NULLS_FORWARD('sales_no_edges', product_id, date, sales_amount);

-- Interpolation
CREATE OR REPLACE TABLE sales_interpolate AS
SELECT * FROM TS_FILL_NULLS_INTERPOLATE('sales_no_edges', product_id, date, sales_amount);

-- Mean fill
CREATE OR REPLACE TABLE sales_mean AS
SELECT * FROM TS_FILL_NULLS_MEAN('sales_no_edges', product_id, date, sales_amount);

SELECT 
    'Original (with nulls)' AS method,
    COUNT(CASE WHEN sales_amount IS NULL THEN 1 END) AS null_count,
    ROUND(AVG(sales_amount), 2) AS avg_value
FROM sales_no_edges
UNION ALL
SELECT 
    'Forward fill',
    COUNT(CASE WHEN sales_amount IS NULL THEN 1 END),
    ROUND(AVG(sales_amount), 2)
FROM sales_forward
UNION ALL
SELECT 
    'Interpolation',
    COUNT(CASE WHEN sales_amount IS NULL THEN 1 END),
    ROUND(AVG(sales_amount), 2)
FROM sales_interpolate
UNION ALL
SELECT 
    'Mean fill',
    COUNT(CASE WHEN sales_amount IS NULL THEN 1 END),
    ROUND(AVG(sales_amount), 2)
FROM sales_mean;

-- 3f. Outlier treatment
SELECT '3f. Outlier treatment' AS example;

-- Cap outliers using IQR
CREATE OR REPLACE TABLE sales_capped AS
SELECT * FROM TS_CAP_OUTLIERS_IQR('sales_interpolate', product_id, date, sales_amount, 1.5);

WITH before_stats AS (
    SELECT 
        MIN(sales_amount) AS min_val,
        MAX(sales_amount) AS max_val,
        ROUND(STDDEV(sales_amount), 2) AS std_val
    FROM sales_interpolate
),
after_stats AS (
    SELECT 
        MIN(sales_amount) AS min_val,
        MAX(sales_amount) AS max_val,
        ROUND(STDDEV(sales_amount), 2) AS std_val
    FROM sales_capped
)
SELECT 'Before capping' AS stage, * FROM before_stats
UNION ALL
SELECT 'After capping', * FROM after_stats;

-- ============================================================================
-- PART 4: Standard Preparation Pipeline
-- ============================================================================

SELECT '=== Part 4: Standard Preparation Pipeline ===' AS section;

SELECT '4a. Apply standard pipeline' AS example;
CREATE OR REPLACE TABLE sales_prepared AS
SELECT * FROM TS_PREPARE_STANDARD(
    'sales_raw',
    product_id,
    date,
    sales_amount,
    365,  -- min_length
    'interpolate'  -- fill_method
);

-- Validate results
CREATE OR REPLACE TABLE prepared_stats AS
SELECT * FROM TS_STATS('sales_prepared', product_id, date, sales_amount);

SELECT '4b. Compare quality scores (before vs after)' AS example;
SELECT 
    'Before preparation' AS stage,
    ROUND(AVG(quality_score), 4) AS avg_quality,
    ROUND(MIN(quality_score), 4) AS min_quality,
    ROUND(MAX(quality_score), 4) AS max_quality,
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END) AS series_with_nulls,
    SUM(CASE WHEN is_constant THEN 1 ELSE 0 END) AS constant_series
FROM sales_stats
UNION ALL
SELECT 
    'After preparation',
    ROUND(AVG(quality_score), 4),
    ROUND(MIN(quality_score), 4),
    ROUND(MAX(quality_score), 4),
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END),
    SUM(CASE WHEN is_constant THEN 1 ELSE 0 END)
FROM prepared_stats;

-- ============================================================================
-- PART 5: Transformations
-- ============================================================================

SELECT '=== Part 5: Transformations ===' AS section;

-- 5a. Normalization
SELECT '5a. Min-Max normalization' AS example;
CREATE OR REPLACE TABLE sales_normalized AS
SELECT * FROM TS_NORMALIZE_MINMAX('sales_prepared', product_id, date, sales_amount);

SELECT 
    product_id,
    ROUND(MIN(sales_amount), 4) AS min_val,
    ROUND(MAX(sales_amount), 4) AS max_val,
    ROUND(AVG(sales_amount), 4) AS avg_val
FROM sales_normalized
GROUP BY product_id
ORDER BY product_id;

-- 5b. Standardization
SELECT '5b. Z-score standardization' AS example;
CREATE OR REPLACE TABLE sales_standardized AS
SELECT * FROM TS_STANDARDIZE('sales_prepared', product_id, date, sales_amount);

SELECT 
    product_id,
    ROUND(AVG(sales_amount), 4) AS mean_val,
    ROUND(STDDEV(sales_amount), 4) AS std_val
FROM sales_standardized
GROUP BY product_id
ORDER BY product_id;

-- 5c. Log transformation
SELECT '5c. Log transformation' AS example;
CREATE OR REPLACE TABLE sales_log AS
SELECT * FROM TS_TRANSFORM_LOG('sales_prepared', product_id, date, sales_amount);

SELECT 
    'Original scale' AS scale,
    ROUND(AVG(sales_amount), 2) AS avg_val,
    ROUND(STDDEV(sales_amount), 2) AS std_val
FROM sales_prepared
WHERE sales_amount > 0
UNION ALL
SELECT 
    'Log scale',
    ROUND(AVG(sales_amount), 2),
    ROUND(STDDEV(sales_amount), 2)
FROM sales_log
WHERE sales_amount IS NOT NULL;

-- 5d. Differencing (1st order)
SELECT '5d. First-order differencing' AS example;
CREATE OR REPLACE TABLE sales_diff AS
SELECT * FROM TS_DIFF('sales_prepared', product_id, date, sales_amount, 1);

WITH diff_stats AS (
    SELECT 
        product_id,
        ROUND(AVG(sales_amount), 2) AS mean_diff,
        ROUND(STDDEV(sales_amount), 2) AS std_diff,
        COUNT(CASE WHEN sales_amount IS NULL THEN 1 END) AS null_count
    FROM sales_diff
    GROUP BY product_id
)
SELECT * FROM diff_stats ORDER BY product_id;

-- ============================================================================
-- PART 6: Forecasting on Prepared Data
-- ============================================================================

SELECT '=== Part 6: Forecasting on Prepared Data ===' AS section;

SELECT '6a. Detect seasonality in prepared data' AS example;
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales_prepared', product_id, date, sales_amount);

SELECT '6b. Generate forecasts' AS example;
CREATE OR REPLACE TABLE forecasts AS
SELECT * FROM TS_FORECAST_BY(
    'sales_prepared',
    product_id,
    date,
    sales_amount,
    'AutoETS',
    28,  -- 4 weeks ahead
    {'seasonal_period': 7, 'return_insample': true, 'confidence_level': 0.95}
);

SELECT 
    product_id,
    forecast_step,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(lower, 2) AS lower_95,
    ROUND(upper, 2) AS upper_95,
    model_name,
    LEN(insample_fitted) AS train_size,
    confidence_level
FROM forecasts
WHERE forecast_step <= 7
ORDER BY product_id, forecast_step;

-- 6c. Evaluate forecast quality with coverage
SELECT '6c. Forecast interval coverage (if we had actuals)' AS example;
SELECT 
    product_id,
    model_name,
    LEN(insample_fitted) AS training_obs,
    confidence_level,
    ROUND(AVG(upper - lower), 2) AS avg_interval_width
FROM forecasts
WHERE forecast_step = 1
GROUP BY product_id, model_name, confidence_level
ORDER BY product_id;

-- ============================================================================
-- PART 7: Custom Pipeline Example
-- ============================================================================

SELECT '=== Part 7: Custom Preparation Pipeline ===' AS section;

SELECT '7a. Multi-step custom pipeline' AS example;
CREATE OR REPLACE TABLE sales_custom_prep AS
WITH 
step1 AS (
    -- Fill gaps
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
),
step2 AS (
    -- Drop constant series
    SELECT * FROM TS_DROP_CONSTANT('step1', product_id, sales_amount)
),
step3 AS (
    -- Drop series with > 20% gaps
    SELECT * FROM TS_DROP_GAPPY('step2', product_id, date, 0.20)
),
step4 AS (
    -- Remove edge zeros
    SELECT * FROM TS_DROP_EDGE_ZEROS('step3', product_id, date, sales_amount)
),
step5 AS (
    -- Fill nulls with interpolation
    SELECT * FROM TS_FILL_NULLS_INTERPOLATE('step4', product_id, date, sales_amount)
),
step6 AS (
    -- Cap outliers
    SELECT * FROM TS_CAP_OUTLIERS_IQR('step5', product_id, date, sales_amount, 2.0)
),
step7 AS (
    -- Normalize
    SELECT * FROM TS_NORMALIZE_MINMAX('step6', product_id, date, sales_amount)
)
SELECT * FROM step7;

-- Verify results
CREATE OR REPLACE TABLE custom_stats AS
SELECT * FROM TS_STATS('sales_custom_prep', product_id, date, sales_amount);

SELECT 
    COUNT(*) AS num_series,
    ROUND(AVG(quality_score), 4) AS avg_quality,
    ROUND(MIN(quality_score), 4) AS min_quality,
    SUM(CASE WHEN n_null > 0 THEN 1 ELSE 0 END) AS series_with_nulls,
    SUM(CASE WHEN n_gaps > 0 THEN 1 ELSE 0 END) AS series_with_gaps
FROM custom_stats;

-- ============================================================================
-- PART 8: Summary Report
-- ============================================================================

SELECT '=== Part 8: Summary Report ===' AS section;

SELECT '8a. Data quality improvement summary' AS example;
WITH pipeline_comparison AS (
    SELECT 
        'Raw data' AS stage,
        COUNT(DISTINCT product_id) AS num_series,
        ROUND(AVG(quality_score), 4) AS avg_quality,
        SUM(CASE WHEN quality_score < 0.5 THEN 1 ELSE 0 END) AS poor_quality_series
    FROM sales_stats
    UNION ALL
    SELECT 
        'After standard prep',
        COUNT(DISTINCT product_id),
        ROUND(AVG(quality_score), 4),
        SUM(CASE WHEN quality_score < 0.5 THEN 1 ELSE 0 END)
    FROM prepared_stats
    UNION ALL
    SELECT 
        'After custom prep',
        COUNT(DISTINCT product_id),
        ROUND(AVG(quality_score), 4),
        SUM(CASE WHEN quality_score < 0.5 THEN 1 ELSE 0 END)
    FROM custom_stats
)
SELECT * FROM pipeline_comparison;

SELECT '8b. Series characteristics' AS example;
SELECT 
    product_id,
    length AS observations,
    n_gaps AS gaps,
    n_null AS nulls,
    n_zeros AS zeros,
    ROUND(mean, 2) AS mean,
    ROUND(std, 2) AS std,
    ROUND(quality_score, 4) AS quality
FROM prepared_stats
ORDER BY quality_score DESC;

SELECT '✅ Demo complete!' AS result;
SELECT 'All EDA and data preparation features demonstrated successfully!' AS summary;

-- Clean up intermediate tables
DROP TABLE IF EXISTS sales_filled;
DROP TABLE IF EXISTS sales_variable;
DROP TABLE IF EXISTS sales_long;
DROP TABLE IF EXISTS sales_no_edges;
DROP TABLE IF EXISTS sales_forward;
DROP TABLE IF EXISTS sales_interpolate;
DROP TABLE IF EXISTS sales_mean;
DROP TABLE IF EXISTS sales_capped;
DROP TABLE IF EXISTS sales_normalized;
DROP TABLE IF EXISTS sales_standardized;
DROP TABLE IF EXISTS sales_log;
DROP TABLE IF EXISTS sales_diff;

