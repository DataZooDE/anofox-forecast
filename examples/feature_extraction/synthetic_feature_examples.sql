-- ============================================================================
-- Feature Extraction Examples - Synthetic Data
-- ============================================================================
-- This script demonstrates time series feature extraction with the anofox-forecast
-- extension using ts_features_by table macro.
--
-- Run: ./build/release/duckdb < examples/feature_extraction/synthetic_feature_examples.sql
-- ============================================================================

-- Load extension
LOAD anofox_forecast;
INSTALL json;
LOAD json;

.print '============================================================================='
.print 'FEATURE EXTRACTION EXAMPLES - Using ts_features_by'
.print '============================================================================='

-- ============================================================================
-- SECTION 1: Basic Feature Extraction for Multiple Series
-- ============================================================================
-- Use ts_features_by to extract features from grouped time series.

.print ''
.print '>>> SECTION 1: Basic Feature Extraction'
.print '-----------------------------------------------------------------------------'

-- Create multi-series data with different patterns
CREATE OR REPLACE TABLE product_data AS
SELECT * FROM (
    -- Product A: Trending upward
    SELECT
        'Product_A' AS product_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        100 + i * 0.5 + (RANDOM() - 0.5) * 10 AS value
    FROM generate_series(0, 29) AS t(i)
    UNION ALL
    -- Product B: Strong weekly seasonality
    SELECT
        'Product_B' AS product_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        200 + 30 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 5 AS value
    FROM generate_series(0, 29) AS t(i)
    UNION ALL
    -- Product C: High volatility (random walk)
    SELECT
        'Product_C' AS product_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        150 + (RANDOM() - 0.5) * 50 AS value
    FROM generate_series(0, 29) AS t(i)
);

.print 'Data overview:'
SELECT product_id, COUNT(*) AS n_rows, ROUND(AVG(value), 2) AS avg_val
FROM product_data GROUP BY product_id ORDER BY product_id;

-- 1.1: Extract all 117 features per series
.print ''
.print 'Section 1.1: All Features per Series'

SELECT
    id,
    mean,
    variance,
    skewness
FROM ts_features_by('product_data', product_id, date, value);

-- ============================================================================
-- SECTION 2: Accessing Specific Features
-- ============================================================================

.print ''
.print '>>> SECTION 2: Accessing Specific Features'
.print '-----------------------------------------------------------------------------'

-- 2.1: Basic statistics
.print 'Section 2.1: Basic Statistics'

SELECT
    id,
    ROUND(mean, 2) AS mean,
    ROUND(median, 2) AS median,
    ROUND(variance, 2) AS variance,
    ROUND(standard_deviation, 2) AS std_dev
FROM ts_features_by('product_data', product_id, date, value);

-- 2.2: Trend features
.print ''
.print 'Section 2.2: Trend Features'

SELECT
    id,
    ROUND(linear_trend_slope, 4) AS trend_slope,
    ROUND(linear_trend_intercept, 2) AS intercept,
    ROUND(linear_trend_r_squared, 4) AS r_squared
FROM ts_features_by('product_data', product_id, date, value);

-- 2.3: Autocorrelation features
.print ''
.print 'Section 2.3: Autocorrelation Features'

SELECT
    id,
    ROUND(autocorrelation_lag1, 4) AS ac_lag1,
    ROUND(autocorrelation_lag7, 4) AS ac_lag7,
    ROUND(partial_autocorrelation_lag1, 4) AS pac_lag1
FROM ts_features_by('product_data', product_id, date, value);

-- 2.4: Distribution features
.print ''
.print 'Section 2.4: Distribution Features'

SELECT
    id,
    ROUND(skewness, 4) AS skewness,
    ROUND(kurtosis, 4) AS kurtosis,
    ROUND(minimum, 2) AS min_val,
    ROUND(maximum, 2) AS max_val
FROM ts_features_by('product_data', product_id, date, value);

-- ============================================================================
-- SECTION 3: Feature Selection
-- ============================================================================

.print ''
.print '>>> SECTION 3: Feature Selection'
.print '-----------------------------------------------------------------------------'

-- 3.1: Extract selected features (select specific columns from full feature set)
.print 'Section 3.1: Selected Features Only'

SELECT
    id,
    ROUND(mean, 2) AS mean,
    ROUND(variance, 2) AS variance,
    ROUND(skewness, 4) AS skewness,
    ROUND(kurtosis, 4) AS kurtosis
FROM ts_features_by('product_data', product_id, date, value);

-- 3.2: Trend and seasonality features
.print ''
.print 'Section 3.2: Trend and Seasonality Features'

SELECT
    id,
    ROUND(linear_trend_slope, 4) AS linear_trend_slope,
    ROUND(linear_trend_r_squared, 4) AS linear_trend_r_squared,
    ROUND(autocorrelation_lag1, 4) AS autocorrelation_lag1
FROM ts_features_by('product_data', product_id, date, value);

-- ============================================================================
-- SECTION 4: Feature-Based Classification
-- ============================================================================

.print ''
.print '>>> SECTION 4: Feature-Based Classification'
.print '-----------------------------------------------------------------------------'

-- Create diverse series with labeled patterns
CREATE OR REPLACE TABLE diverse_series AS
SELECT * FROM (
    -- Trending series
    SELECT
        'trending_' || i AS series_id,
        'trending' AS actual_pattern,
        '2024-01-01'::TIMESTAMP + INTERVAL (j) DAY AS date,
        50 + j * 2 + (RANDOM() - 0.5) * 5 AS value
    FROM generate_series(1, 3) AS t(i),
         generate_series(0, 29) AS s(j)
    UNION ALL
    -- Seasonal series
    SELECT
        'seasonal_' || i AS series_id,
        'seasonal' AS actual_pattern,
        '2024-01-01'::TIMESTAMP + INTERVAL (j) DAY AS date,
        100 + 30 * SIN(2 * PI() * j / 7) + (RANDOM() - 0.5) * 5 AS value
    FROM generate_series(1, 3) AS t(i),
         generate_series(0, 29) AS s(j)
    UNION ALL
    -- Volatile series
    SELECT
        'volatile_' || i AS series_id,
        'volatile' AS actual_pattern,
        '2024-01-01'::TIMESTAMP + INTERVAL (j) DAY AS date,
        100 + (RANDOM() - 0.5) * 80 AS value
    FROM generate_series(1, 3) AS t(i),
         generate_series(0, 29) AS s(j)
);

.print 'Patterns in dataset:'
SELECT actual_pattern, COUNT(DISTINCT series_id) AS n_series
FROM diverse_series GROUP BY actual_pattern ORDER BY actual_pattern;

-- 4.1: Extract classification features
.print ''
.print 'Section 4.1: Classification Features per Series'

SELECT
    id,
    ROUND(linear_trend_slope, 4) AS trend,
    ROUND(variance, 2) AS variance,
    ROUND(autocorrelation_lag7, 4) AS ac7
FROM ts_features_by('diverse_series', series_id, date, value);

-- 4.2: Feature-based pattern classification
.print ''
.print 'Section 4.2: Pattern Classification Using Features'

WITH feature_data AS (
    SELECT
        id,
        linear_trend_slope AS trend,
        variance AS variance,
        autocorrelation_lag7 AS ac7
    FROM ts_features_by('diverse_series', series_id, date, value)
)
SELECT
    f.id AS series,
    d.actual_pattern,
    CASE
        WHEN f.trend > 1.0 THEN 'trending'
        WHEN f.ac7 > 0.5 THEN 'seasonal'
        WHEN f.variance > 400 THEN 'volatile'
        ELSE 'other'
    END AS predicted_pattern,
    ROUND(f.trend, 4) AS trend,
    ROUND(f.variance, 2) AS variance,
    ROUND(f.ac7, 4) AS ac7
FROM feature_data f
JOIN (SELECT DISTINCT series_id, actual_pattern FROM diverse_series) d ON f.id = d.series_id
ORDER BY d.actual_pattern, f.id;

-- ============================================================================
-- SECTION 5: Comparing Series by Features
-- ============================================================================

.print ''
.print '>>> SECTION 5: Comparing Series by Features'
.print '-----------------------------------------------------------------------------'

.print 'Feature Summary by Pattern:'

WITH feature_data AS (
    SELECT
        id,
        linear_trend_slope AS trend,
        variance AS variance,
        autocorrelation_lag1 AS ac1,
        autocorrelation_lag7 AS ac7
    FROM ts_features_by('diverse_series', series_id, date, value)
),
with_pattern AS (
    SELECT f.*, d.actual_pattern
    FROM feature_data f
    JOIN (SELECT DISTINCT series_id, actual_pattern FROM diverse_series) d ON f.id = d.series_id
)
SELECT
    actual_pattern,
    ROUND(AVG(trend), 4) AS avg_trend,
    ROUND(AVG(variance), 2) AS avg_variance,
    ROUND(AVG(ac1), 4) AS avg_ac1,
    ROUND(AVG(ac7), 4) AS avg_ac7
FROM with_pattern
GROUP BY actual_pattern
ORDER BY actual_pattern;

-- ============================================================================
-- CLEANUP
-- ============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS product_data;
DROP TABLE IF EXISTS diverse_series;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'FEATURE EXTRACTION EXAMPLES COMPLETE'
.print '============================================================================='
