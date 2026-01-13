-- =============================================================================
-- Feature Extraction Examples - Synthetic Data
-- =============================================================================
-- This script demonstrates time series feature extraction with the anofox-forecast
-- extension using 6 patterns from basic to advanced.
--
-- Run: ./build/release/duckdb < examples/feature_extraction/synthetic_feature_examples.sql
-- =============================================================================

-- Load extension
LOAD anofox_forecast;

.print '============================================================================='
.print 'FEATURE EXTRACTION EXAMPLES - Synthetic Data'
.print '============================================================================='

-- =============================================================================
-- SECTION 1: Quick Start (Basic Extraction)
-- =============================================================================
-- Use case: Extract all features from a single series.

.print ''
.print '>>> SECTION 1: Quick Start (Basic Extraction)'
.print '-----------------------------------------------------------------------------'

-- Create a simple time series
CREATE OR REPLACE TABLE simple_series AS
SELECT
    '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
    (i + 1.0)::DOUBLE AS value
FROM generate_series(0, 9) AS t(i);

.print 'Input data:'
SELECT * FROM simple_series;

.print ''
.print 'Extract all 117 features:'
SELECT ts_features(ts, value) AS features FROM simple_series;

-- =============================================================================
-- SECTION 2: Access Specific Features
-- =============================================================================
-- Use case: Extract and use individual features.

.print ''
.print '>>> SECTION 2: Access Specific Features'
.print '-----------------------------------------------------------------------------'

-- Create sample data with trend
CREATE OR REPLACE TABLE sample_data AS
SELECT
    '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
    CASE i
        WHEN 0 THEN 10.0 WHEN 1 THEN 12.0 WHEN 2 THEN 14.0 WHEN 3 THEN 13.0 WHEN 4 THEN 15.0
        WHEN 5 THEN 18.0 WHEN 6 THEN 16.0 WHEN 7 THEN 19.0 WHEN 8 THEN 21.0 WHEN 9 THEN 20.0
        WHEN 10 THEN 22.0 WHEN 11 THEN 25.0 WHEN 12 THEN 23.0 WHEN 13 THEN 26.0 WHEN 14 THEN 28.0
        WHEN 15 THEN 27.0 WHEN 16 THEN 30.0 WHEN 17 THEN 32.0 WHEN 18 THEN 31.0 ELSE 34.0
    END AS value
FROM generate_series(0, 19) AS t(i);

.print 'Basic statistics:'
SELECT
    (ts_features(ts, value)).mean AS mean,
    (ts_features(ts, value)).median AS median,
    (ts_features(ts, value)).variance AS variance,
    (ts_features(ts, value)).standard_deviation AS std_dev
FROM sample_data;

.print ''
.print 'Distribution features:'
SELECT
    (ts_features(ts, value)).skewness AS skewness,
    (ts_features(ts, value)).kurtosis AS kurtosis,
    (ts_features(ts, value)).minimum AS min_val,
    (ts_features(ts, value)).maximum AS max_val,
    (ts_features(ts, value))."range" AS range
FROM sample_data;

.print ''
.print 'Trend features:'
SELECT
    (ts_features(ts, value)).linear_trend_slope AS trend_slope,
    (ts_features(ts, value)).linear_trend_intercept AS trend_intercept,
    (ts_features(ts, value)).linear_trend_r_squared AS trend_r2
FROM sample_data;

.print ''
.print 'Autocorrelation features:'
SELECT
    (ts_features(ts, value)).autocorrelation_lag1 AS ac_lag1,
    (ts_features(ts, value)).autocorrelation_lag2 AS ac_lag2,
    (ts_features(ts, value)).autocorrelation_lag3 AS ac_lag3,
    (ts_features(ts, value)).partial_autocorrelation_lag1 AS pac_lag1
FROM sample_data;

.print ''
.print 'Entropy and complexity:'
SELECT
    (ts_features(ts, value)).sample_entropy AS sample_ent,
    (ts_features(ts, value)).permutation_entropy AS perm_ent,
    (ts_features(ts, value)).binned_entropy AS binned_ent,
    (ts_features(ts, value)).cid_ce AS complexity
FROM sample_data;

-- =============================================================================
-- SECTION 3: Multi-Series Feature Extraction
-- =============================================================================
-- Use case: Extract features for multiple time series in one query.

.print ''
.print '>>> SECTION 3: Multi-Series Feature Extraction'
.print '-----------------------------------------------------------------------------'

-- Create multi-series data
CREATE OR REPLACE TABLE multi_series AS
SELECT
    CASE WHEN i % 3 = 0 THEN 'Product_A'
         WHEN i % 3 = 1 THEN 'Product_B'
         ELSE 'Product_C' END AS product_id,
    '2024-01-01'::TIMESTAMP + INTERVAL (i / 3) DAY AS date,
    CASE WHEN i % 3 = 0 THEN 100 + (i / 3) * 0.5 + (RANDOM() - 0.5) * 10
         WHEN i % 3 = 1 THEN 200 + 30 * SIN(2 * PI() * (i / 3) / 7) + (RANDOM() - 0.5) * 5
         ELSE 150 + (RANDOM() - 0.5) * 50 END AS value
FROM generate_series(0, 89) AS t(i);

.print 'Data overview:'
SELECT product_id, COUNT(*) AS n_rows, ROUND(AVG(value), 2) AS avg_val
FROM multi_series GROUP BY product_id ORDER BY product_id;

-- Extract features using aggregate function
.print ''
.print 'Feature extraction per product (aggregate function):'
SELECT
    product_id,
    ts_features(date, value) AS features
FROM multi_series
GROUP BY product_id
ORDER BY product_id;

-- Extract specific features per product
.print ''
.print 'Selected features per product:'
SELECT
    product_id,
    ROUND((ts_features(date, value)).mean, 2) AS mean,
    ROUND((ts_features(date, value)).variance, 2) AS variance,
    ROUND((ts_features(date, value)).linear_trend_slope, 4) AS trend,
    ROUND((ts_features(date, value)).autocorrelation_lag1, 4) AS ac1
FROM multi_series
GROUP BY product_id
ORDER BY product_id;

-- =============================================================================
-- SECTION 4: Feature Selection
-- =============================================================================
-- Use case: Extract only specific features for efficiency.

.print ''
.print '>>> SECTION 4: Feature Selection'
.print '-----------------------------------------------------------------------------'

-- Create data
CREATE OR REPLACE TABLE feature_select_data AS
SELECT
    'series_1' AS id,
    '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
    100 + i * 0.3 + 20 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 10 AS value
FROM generate_series(0, 59) AS t(i);

-- Extract only selected features
.print 'Extract specific features only:'
SELECT
    id,
    ts_features(date, value, ['mean', 'variance', 'skewness', 'kurtosis']) AS features
FROM feature_select_data
GROUP BY id;

.print ''
.print 'Extract trend and autocorrelation features:'
SELECT
    id,
    ts_features(date, value,
        ['linear_trend_slope', 'linear_trend_r_squared', 'autocorrelation_lag1', 'autocorrelation_lag7']
    ) AS features
FROM feature_select_data
GROUP BY id;

-- =============================================================================
-- SECTION 5: List Available Features
-- =============================================================================
-- Use case: Discover all available features and their parameters.

.print ''
.print '>>> SECTION 5: List Available Features'
.print '-----------------------------------------------------------------------------'

.print 'First 20 available features:'
SELECT feature_name FROM ts_features_list() LIMIT 20;

.print ''
.print 'Total feature count:'
SELECT COUNT(*) AS total_features FROM ts_features_list();

.print ''
.print 'Feature details (first 10):'
SELECT * FROM ts_features_list() LIMIT 10;

-- =============================================================================
-- SECTION 6: Feature-Based Classification
-- =============================================================================
-- Use case: Use features to classify or cluster time series.

.print ''
.print '>>> SECTION 6: Feature-Based Classification'
.print '-----------------------------------------------------------------------------'

-- Create diverse series with different characteristics
CREATE OR REPLACE TABLE diverse_series AS
SELECT * FROM (
    -- Trending series
    SELECT
        'trending' AS pattern,
        'series_' || i AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (j) DAY AS date,
        50 + j * 2 + (RANDOM() - 0.5) * 5 AS value
    FROM generate_series(1, 3) AS t(i),
         generate_series(0, 29) AS s(j)
    UNION ALL
    -- Seasonal series
    SELECT
        'seasonal' AS pattern,
        'series_' || (i + 3) AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (j) DAY AS date,
        100 + 30 * SIN(2 * PI() * j / 7) + (RANDOM() - 0.5) * 5 AS value
    FROM generate_series(1, 3) AS t(i),
         generate_series(0, 29) AS s(j)
    UNION ALL
    -- Volatile series
    SELECT
        'volatile' AS pattern,
        'series_' || (i + 6) AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (j) DAY AS date,
        100 + (RANDOM() - 0.5) * 80 AS value
    FROM generate_series(1, 3) AS t(i),
         generate_series(0, 29) AS s(j)
);

.print 'Series patterns:'
SELECT pattern, COUNT(DISTINCT series_id) AS n_series
FROM diverse_series GROUP BY pattern ORDER BY pattern;

-- Extract features and analyze patterns
.print ''
.print 'Features by pattern:'
WITH per_series_features AS (
    SELECT
        pattern,
        series_id,
        (ts_features(date, value)).linear_trend_slope AS trend_slope,
        (ts_features(date, value)).variance AS variance,
        (ts_features(date, value)).autocorrelation_lag1 AS ac1,
        (ts_features(date, value)).autocorrelation_lag7 AS ac7
    FROM diverse_series
    GROUP BY pattern, series_id
)
SELECT
    pattern,
    ROUND(AVG(trend_slope), 4) AS avg_trend,
    ROUND(AVG(variance), 2) AS avg_variance,
    ROUND(AVG(ac1), 4) AS avg_ac1,
    ROUND(AVG(ac7), 4) AS avg_ac7
FROM per_series_features
GROUP BY pattern
ORDER BY pattern;

-- Classify series based on features
.print ''
.print 'Feature-based classification:'
WITH series_features AS (
    SELECT
        series_id,
        pattern AS actual_pattern,
        (ts_features(date, value)).linear_trend_slope AS trend,
        (ts_features(date, value)).variance AS variance,
        (ts_features(date, value)).autocorrelation_lag1 AS ac1,
        (ts_features(date, value)).autocorrelation_lag7 AS ac7
    FROM diverse_series
    GROUP BY series_id, pattern
)
SELECT
    series_id,
    actual_pattern,
    CASE
        WHEN trend > 1.0 THEN 'trending'
        WHEN ac7 > 0.5 THEN 'seasonal'
        WHEN variance > 400 THEN 'volatile'
        ELSE 'other'
    END AS predicted_pattern,
    ROUND(trend, 4) AS trend,
    ROUND(variance, 2) AS variance,
    ROUND(ac7, 4) AS ac7
FROM series_features
ORDER BY actual_pattern, series_id;

-- =============================================================================
-- CLEANUP
-- =============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS simple_series;
DROP TABLE IF EXISTS sample_data;
DROP TABLE IF EXISTS multi_series;
DROP TABLE IF EXISTS feature_select_data;
DROP TABLE IF EXISTS diverse_series;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'FEATURE EXTRACTION EXAMPLES COMPLETE'
.print '============================================================================='
