-- =============================================================================
-- Data Preparation Examples - Synthetic Data
-- =============================================================================
-- This script demonstrates time series data preparation with the anofox-forecast
-- extension using table macros for filtering and cleaning.
--
-- Run: ./build/release/duckdb < examples/data_preparation/synthetic_data_prep_examples.sql
-- =============================================================================

-- Load extension
LOAD anofox_forecast;

.print '============================================================================='
.print 'DATA PREPARATION EXAMPLES - Synthetic Data'
.print '============================================================================='

-- =============================================================================
-- SECTION 1: Drop Constant Series
-- =============================================================================
-- Use case: Remove series with no variation (can't be forecast).

.print ''
.print '>>> SECTION 1: Drop Constant Series'
.print '-----------------------------------------------------------------------------'

-- Create test data with constant and variable series
CREATE OR REPLACE TABLE constant_test AS
SELECT * FROM (
    -- Constant series (should be dropped)
    SELECT 'A' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts, 5.0 AS value
    FROM generate_series(0, 9) t(i)
    UNION ALL
    -- Variable series (should be kept)
    SELECT 'B' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts, (i + 1.0) AS value
    FROM generate_series(0, 9) t(i)
    UNION ALL
    -- Another constant series
    SELECT 'C' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts, 10.0 AS value
    FROM generate_series(0, 9) t(i)
);

.print 'Original data summary:'
SELECT series_id, COUNT(*) AS n_points, MIN(value) AS min_val, MAX(value) AS max_val
FROM constant_test GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_drop_constant (constant series removed):'
SELECT series_id, COUNT(*) AS n_points, MIN(value) AS min_val, MAX(value) AS max_val
FROM ts_drop_constant('constant_test', series_id, value)
GROUP BY series_id ORDER BY series_id;

-- =============================================================================
-- SECTION 2: Drop Short Series
-- =============================================================================
-- Use case: Remove series too short for reliable forecasting.

.print ''
.print '>>> SECTION 2: Drop Short Series'
.print '-----------------------------------------------------------------------------'

-- Create test data with different length series
CREATE OR REPLACE TABLE length_test AS
SELECT * FROM (
    -- Short series (3 points)
    SELECT 'A' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts, (i + 1.0) AS value
    FROM generate_series(0, 2) t(i)
    UNION ALL
    -- Medium series (7 points)
    SELECT 'B' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts, (i + 10.0) AS value
    FROM generate_series(0, 6) t(i)
    UNION ALL
    -- Long series (15 points)
    SELECT 'C' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts, (i + 20.0) AS value
    FROM generate_series(0, 14) t(i)
);

.print 'Original data summary:'
SELECT series_id, COUNT(*) AS n_points FROM length_test GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_drop_short (min_length=5):'
SELECT series_id, COUNT(*) AS n_points
FROM ts_drop_short('length_test', series_id, 5)
GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_drop_short (min_length=10):'
SELECT series_id, COUNT(*) AS n_points
FROM ts_drop_short('length_test', series_id, 10)
GROUP BY series_id ORDER BY series_id;

-- =============================================================================
-- SECTION 3: Clean Edge Zeros
-- =============================================================================
-- Use case: Remove leading/trailing zeros from demand data.

.print ''
.print '>>> SECTION 3: Clean Edge Zeros'
.print '-----------------------------------------------------------------------------'

-- Create test data with edge zeros
CREATE OR REPLACE TABLE edge_zeros_test AS
SELECT * FROM (
    -- Leading zeros
    SELECT 'A' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i < 3 THEN 0.0 ELSE (i - 2.0) END AS value
    FROM generate_series(0, 7) t(i)
    UNION ALL
    -- Trailing zeros
    SELECT 'B' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i > 4 THEN 0.0 ELSE (i + 1.0) END AS value
    FROM generate_series(0, 7) t(i)
    UNION ALL
    -- Both edge zeros
    SELECT 'C' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i < 2 OR i > 5 THEN 0.0 ELSE (i * 2.0) END AS value
    FROM generate_series(0, 7) t(i)
);

.print 'Original data with edge zeros:'
SELECT series_id, LIST(value ORDER BY ts) AS values FROM edge_zeros_test GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_drop_leading_zeros:'
SELECT series_id, COUNT(*) AS n_points, LIST(value ORDER BY ts) AS values
FROM ts_drop_leading_zeros('edge_zeros_test', series_id, ts, value)
GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_drop_trailing_zeros:'
SELECT series_id, COUNT(*) AS n_points, LIST(value ORDER BY ts) AS values
FROM ts_drop_trailing_zeros('edge_zeros_test', series_id, ts, value)
GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_drop_edge_zeros (both ends):'
SELECT series_id, COUNT(*) AS n_points, LIST(value ORDER BY ts) AS values
FROM ts_drop_edge_zeros('edge_zeros_test', series_id, ts, value)
GROUP BY series_id ORDER BY series_id;

-- =============================================================================
-- SECTION 4: Fill Missing Values (Imputation)
-- =============================================================================
-- Use case: Handle NULLs with various strategies.

.print ''
.print '>>> SECTION 4: Fill Missing Values (Imputation)'
.print '-----------------------------------------------------------------------------'

-- Create test data with NULLs
CREATE OR REPLACE TABLE null_test AS
SELECT * FROM (
    SELECT 'A' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i IN (1, 2, 5) THEN NULL ELSE (i * 10.0) END AS value
    FROM generate_series(0, 7) t(i)
    UNION ALL
    SELECT 'B' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i IN (0, 1, 6, 7) THEN NULL ELSE (i + 100.0) END AS value
    FROM generate_series(0, 7) t(i)
);

.print 'Original data with NULLs:'
SELECT series_id, LIST(value ORDER BY ts) AS values FROM null_test GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_fill_nulls_forward (LOCF):'
SELECT series_id, LIST(value_col ORDER BY ts) AS values
FROM ts_fill_nulls_forward('null_test', series_id, ts, value)
GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_fill_nulls_backward:'
SELECT series_id, LIST(value_col ORDER BY ts) AS values
FROM ts_fill_nulls_backward('null_test', series_id, ts, value)
GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_fill_nulls_mean:'
SELECT series_id, LIST(ROUND(value_col, 2) ORDER BY ts) AS values
FROM ts_fill_nulls_mean('null_test', series_id, ts, value)
GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_fill_nulls_const (fill with 0):'
SELECT series_id, LIST(value_col ORDER BY ts) AS values
FROM ts_fill_nulls_const('null_test', series_id, ts, value, 0)
GROUP BY series_id ORDER BY series_id;

-- =============================================================================
-- SECTION 5: Differencing
-- =============================================================================
-- Use case: Make series stationary for ARIMA models.

.print ''
.print '>>> SECTION 5: Differencing'
.print '-----------------------------------------------------------------------------'

-- Create data with trend
CREATE OR REPLACE TABLE trend_test AS
SELECT
    'A' AS series_id,
    '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
    (100.0 + i * 5.0)::DOUBLE AS value  -- linear trend
FROM generate_series(0, 9) t(i);

.print 'Original data with trend:'
SELECT series_id, LIST(value ORDER BY ts) AS values FROM trend_test GROUP BY series_id;

.print ''
.print 'After ts_diff (first difference, removes linear trend):'
SELECT series_id, LIST(ROUND(diff_value, 2) ORDER BY ts) AS values
FROM ts_diff('trend_test', series_id, ts, value, 1)
GROUP BY series_id;

-- =============================================================================
-- SECTION 6: Complete Pipeline
-- =============================================================================
-- Use case: Chain multiple preparation steps.

.print ''
.print '>>> SECTION 6: Complete Pipeline'
.print '-----------------------------------------------------------------------------'

-- Create messy data
CREATE OR REPLACE TABLE messy_data AS
SELECT * FROM (
    -- Good series with some NULLs
    SELECT 'GOOD' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i = 3 THEN NULL ELSE (i + 10.0) END AS value
    FROM generate_series(0, 9) t(i)
    UNION ALL
    -- Constant series (should be filtered)
    SELECT 'CONST' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           5.0 AS value
    FROM generate_series(0, 9) t(i)
    UNION ALL
    -- Short series (should be filtered)
    SELECT 'SHORT' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           (i + 1.0) AS value
    FROM generate_series(0, 2) t(i)
    UNION ALL
    -- Series with edge zeros
    SELECT 'EDGES' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i < 2 OR i > 7 THEN 0.0 ELSE (i * 5.0) END AS value
    FROM generate_series(0, 9) t(i)
);

.print 'Original messy data:'
SELECT series_id, COUNT(*) AS n, LIST(value ORDER BY ts) AS values
FROM messy_data GROUP BY series_id ORDER BY series_id;

-- Step 1: Fill NULLs (output has value_col column)
.print ''
.print 'Step 1 - After filling NULLs:'
CREATE OR REPLACE TABLE step1 AS
SELECT series_id, ts, value_col AS value
FROM ts_fill_nulls_forward('messy_data', series_id, ts, value);

SELECT series_id, COUNT(*) AS n, LIST(value ORDER BY ts) AS values
FROM step1 GROUP BY series_id ORDER BY series_id;

-- Step 2: Remove edge zeros
.print ''
.print 'Step 2 - After removing edge zeros:'
CREATE OR REPLACE TABLE step2 AS
SELECT * FROM ts_drop_edge_zeros('step1', series_id, ts, value);

SELECT series_id, COUNT(*) AS n, LIST(value ORDER BY ts) AS values
FROM step2 GROUP BY series_id ORDER BY series_id;

-- Step 3: Remove constant series
.print ''
.print 'Step 3 - After removing constant series:'
CREATE OR REPLACE TABLE step3 AS
SELECT * FROM ts_drop_constant('step2', series_id, value);

SELECT series_id, COUNT(*) AS n, LIST(value ORDER BY ts) AS values
FROM step3 GROUP BY series_id ORDER BY series_id;

-- Step 4: Remove short series
.print ''
.print 'Step 4 - After removing short series (min_length=5):'
SELECT series_id, COUNT(*) AS n, LIST(value ORDER BY ts) AS values
FROM ts_drop_short('step3', series_id, 5)
GROUP BY series_id ORDER BY series_id;

-- =============================================================================
-- CLEANUP
-- =============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS constant_test;
DROP TABLE IF EXISTS length_test;
DROP TABLE IF EXISTS edge_zeros_test;
DROP TABLE IF EXISTS null_test;
DROP TABLE IF EXISTS trend_test;
DROP TABLE IF EXISTS messy_data;
DROP TABLE IF EXISTS step1;
DROP TABLE IF EXISTS step2;
DROP TABLE IF EXISTS step3;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'DATA PREPARATION EXAMPLES COMPLETE'
.print '============================================================================='
