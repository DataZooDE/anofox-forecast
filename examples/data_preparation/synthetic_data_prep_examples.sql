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
FROM ts_drop_constant_by('constant_test', series_id, value)
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
FROM ts_drop_short_by('length_test', series_id, 5)
GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_drop_short (min_length=10):'
SELECT series_id, COUNT(*) AS n_points
FROM ts_drop_short_by('length_test', series_id, 10)
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
FROM ts_drop_leading_zeros_by('edge_zeros_test', series_id, ts, value)
GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_drop_trailing_zeros:'
SELECT series_id, COUNT(*) AS n_points, LIST(value ORDER BY ts) AS values
FROM ts_drop_trailing_zeros_by('edge_zeros_test', series_id, ts, value)
GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_drop_edge_zeros (both ends):'
SELECT series_id, COUNT(*) AS n_points, LIST(value ORDER BY ts) AS values
FROM ts_drop_edge_zeros_by('edge_zeros_test', series_id, ts, value)
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
FROM ts_fill_nulls_forward_by('null_test', series_id, ts, value)
GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_fill_nulls_backward:'
SELECT series_id, LIST(value_col ORDER BY ts) AS values
FROM ts_fill_nulls_backward_by('null_test', series_id, ts, value)
GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_fill_nulls_mean:'
SELECT series_id, LIST(ROUND(value_col, 2) ORDER BY ts) AS values
FROM ts_fill_nulls_mean_by('null_test', series_id, ts, value)
GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_fill_nulls_const (fill with 0):'
SELECT series_id, LIST(value_col ORDER BY ts) AS values
FROM ts_fill_nulls_const_by('null_test', series_id, ts, value, 0)
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
FROM ts_diff_by('trend_test', series_id, ts, value, 1)
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
FROM ts_fill_nulls_forward_by('messy_data', series_id, ts, value);

SELECT series_id, COUNT(*) AS n, LIST(value ORDER BY ts) AS values
FROM step1 GROUP BY series_id ORDER BY series_id;

-- Step 2: Remove edge zeros
.print ''
.print 'Step 2 - After removing edge zeros:'
CREATE OR REPLACE TABLE step2 AS
SELECT * FROM ts_drop_edge_zeros_by('step1', series_id, ts, value);

SELECT series_id, COUNT(*) AS n, LIST(value ORDER BY ts) AS values
FROM step2 GROUP BY series_id ORDER BY series_id;

-- Step 3: Remove constant series
.print ''
.print 'Step 3 - After removing constant series:'
CREATE OR REPLACE TABLE step3 AS
SELECT * FROM ts_drop_constant_by('step2', series_id, value);

SELECT series_id, COUNT(*) AS n, LIST(value ORDER BY ts) AS values
FROM step3 GROUP BY series_id ORDER BY series_id;

-- Step 4: Remove short series
.print ''
.print 'Step 4 - After removing short series (min_length=5):'
SELECT series_id, COUNT(*) AS n, LIST(value ORDER BY ts) AS values
FROM ts_drop_short_by('step3', series_id, 5)
GROUP BY series_id ORDER BY series_id;

-- =============================================================================
-- SECTION 7: Statistics Summary
-- =============================================================================
-- Use case: Get a quick overview of dataset characteristics.
-- Compare: ts_stats (per-series) vs ts_stats_summary (dataset-wide aggregate)

.print ''
.print '>>> SECTION 7: Statistics Summary'
.print '-----------------------------------------------------------------------------'

-- Create test data with varying characteristics
CREATE OR REPLACE TABLE stats_test AS
SELECT * FROM (
    -- Short series with gaps
    SELECT 'A' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i = 2 THEN NULL ELSE (i * 10.0) END AS value
    FROM generate_series(0, 4) t(i)
    UNION ALL
    -- Longer series, no gaps
    SELECT 'B' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           (i + 100.0) AS value
    FROM generate_series(0, 9) t(i)
    UNION ALL
    -- Medium series with multiple gaps
    SELECT 'C' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i IN (1, 3, 5) THEN NULL ELSE (i * 5.0) END AS value
    FROM generate_series(0, 7) t(i)
);

.print 'Per-series stats with ts_stats (detailed):'
SELECT id AS series_id,
       (stats).length AS length,
       (stats).n_nulls AS nulls,
       (stats).n_zeros AS zeros
FROM ts_stats_by('stats_test', series_id, ts, value, '1d');

.print ''
.print 'Dataset-wide summary (aggregated manually):'
-- First compute stats, then summarize
CREATE OR REPLACE TABLE computed_stats AS
SELECT * FROM ts_stats_by('stats_test', series_id, ts, value, '1d');

-- Manual aggregation (ts_stats_summary currently has a bug with n_gaps field)
SELECT
    COUNT(*) AS n_series,
    ROUND(AVG((stats).length), 1) AS avg_length,
    MIN((stats).length) AS min_length,
    MAX((stats).length) AS max_length,
    SUM((stats).n_nulls) AS total_nulls,
    SUM((stats).n_zeros) AS total_zeros
FROM computed_stats;

.print ''
.print 'When to use: ts_stats for investigating individual series,'
.print '             ts_stats_summary for quick dataset health check.'

-- =============================================================================
-- SECTION 8: Data Quality Summary
-- =============================================================================
-- Use case: Quickly assess data quality distribution across all series.
-- Compare: ts_data_quality (per-series) vs ts_data_quality_summary (tier counts)

.print ''
.print '>>> SECTION 8: Data Quality Summary'
.print '-----------------------------------------------------------------------------'

-- Reuse stats_test data, add more variation
CREATE OR REPLACE TABLE quality_test AS
SELECT * FROM (
    -- Good quality series (long, few nulls)
    SELECT 'GOOD1' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           (i * 10.0 + SIN(i)) AS value
    FROM generate_series(0, 29) t(i)
    UNION ALL
    SELECT 'GOOD2' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           (i * 5.0 + 50.0) AS value
    FROM generate_series(0, 24) t(i)
    UNION ALL
    -- Fair quality series (some nulls)
    SELECT 'FAIR1' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i % 5 = 0 THEN NULL ELSE (i * 2.0) END AS value
    FROM generate_series(0, 19) t(i)
    UNION ALL
    -- Poor quality series (short, many nulls)
    SELECT 'POOR1' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i % 2 = 0 THEN NULL ELSE (i + 1.0) END AS value
    FROM generate_series(0, 5) t(i)
    UNION ALL
    SELECT 'POOR2' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i < 2 THEN NULL ELSE (i * 3.0) END AS value
    FROM generate_series(0, 3) t(i)
);

.print 'Per-series quality with ts_data_quality (detailed):'
SELECT
    unique_id,
    ROUND((quality).overall_score, 2) AS score,
    CASE
        WHEN (quality).overall_score >= 0.8 THEN 'GOOD'
        WHEN (quality).overall_score >= 0.5 THEN 'FAIR'
        ELSE 'POOR'
    END AS tier
FROM ts_data_quality_by('quality_test', series_id, ts, value, 10, '1d');

.print ''
.print 'Dataset-wide summary with ts_data_quality_summary (tier counts):'
SELECT * FROM ts_data_quality_summary('quality_test', series_id, ts, value, 10);

.print ''
.print 'When to use: ts_data_quality to identify which series need attention,'
.print '             ts_data_quality_summary for quick pass/fail assessment.'

-- =============================================================================
-- SECTION 9: Drop Zeros vs Drop Gappy
-- =============================================================================
-- Use case: Filter out series that are inactive (all zeros) vs sparse (many nulls).
-- Compare: ts_drop_zeros (all-zero filter) vs ts_drop_gappy (configurable null threshold)

.print ''
.print '>>> SECTION 9: Drop Zeros vs Drop Gappy'
.print '-----------------------------------------------------------------------------'

CREATE OR REPLACE TABLE filter_test AS
SELECT * FROM (
    -- Active series (has non-zero values)
    SELECT 'ACTIVE' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           (i + 1.0) AS value
    FROM generate_series(0, 9) t(i)
    UNION ALL
    -- Inactive series (all zeros)
    SELECT 'ZEROS' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           0.0 AS value
    FROM generate_series(0, 9) t(i)
    UNION ALL
    -- Sparse series (30% nulls)
    SELECT 'SPARSE30' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i IN (0, 3, 6) THEN NULL ELSE (i * 5.0) END AS value
    FROM generate_series(0, 9) t(i)
    UNION ALL
    -- Very sparse series (60% nulls)
    SELECT 'SPARSE60' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i IN (0, 1, 3, 4, 6, 7) THEN NULL ELSE (i * 5.0) END AS value
    FROM generate_series(0, 9) t(i)
    UNION ALL
    -- Mixed: some zeros, some nulls
    SELECT 'MIXED' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           CASE WHEN i < 3 THEN 0.0 WHEN i = 5 THEN NULL ELSE (i * 2.0) END AS value
    FROM generate_series(0, 9) t(i)
);

.print 'Original data summary:'
SELECT series_id,
       COUNT(*) AS n_rows,
       SUM(CASE WHEN value = 0 THEN 1 ELSE 0 END) AS n_zeros,
       SUM(CASE WHEN value IS NULL THEN 1 ELSE 0 END) AS n_nulls,
       SUM(CASE WHEN value != 0 AND value IS NOT NULL THEN 1 ELSE 0 END) AS n_valid
FROM filter_test GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_drop_zeros (removes series with NO non-zero values):'
SELECT DISTINCT series_id FROM ts_drop_zeros_by('filter_test', series_id, value) ORDER BY series_id;

.print ''
.print 'After ts_drop_gappy max_ratio=0.2 (removes series with >20% nulls):'
SELECT DISTINCT series_id FROM ts_drop_gappy_by('filter_test', series_id, value, 0.2) ORDER BY series_id;

.print ''
.print 'After ts_drop_gappy max_ratio=0.5 (removes series with >50% nulls):'
SELECT DISTINCT series_id FROM ts_drop_gappy_by('filter_test', series_id, value, 0.5) ORDER BY series_id;

.print ''
.print 'When to use: ts_drop_zeros for removing truly inactive series,'
.print '             ts_drop_gappy for configurable sparsity threshold.'

-- =============================================================================
-- SECTION 10: Timestamp Validation
-- =============================================================================
-- Use case: Verify all expected timestamps exist before forecasting.
-- Compare: ts_validate_timestamps (per-group detail) vs ts_validate_timestamps_summary (pass/fail)

.print ''
.print '>>> SECTION 10: Timestamp Validation'
.print '-----------------------------------------------------------------------------'

-- Create test data with missing timestamps
CREATE OR REPLACE TABLE timestamp_test AS
SELECT * FROM (
    -- Complete series
    SELECT 'COMPLETE' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           (i + 1.0) AS value
    FROM generate_series(0, 4) t(i)
    UNION ALL
    -- Missing middle timestamps (Jan 2, 3)
    SELECT 'MISSING_MID' AS series_id, ts, value FROM (VALUES
        ('2024-01-01'::TIMESTAMP, 10.0),
        ('2024-01-04'::TIMESTAMP, 40.0),
        ('2024-01-05'::TIMESTAMP, 50.0)
    ) AS t(ts, value)
    UNION ALL
    -- Missing end timestamp (Jan 5)
    SELECT 'MISSING_END' AS series_id, '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
           (i + 1.0) AS value
    FROM generate_series(0, 3) t(i)
);

-- Define expected timestamps
.print 'Expected timestamps: 2024-01-01 through 2024-01-05'

.print ''
.print 'Per-group validation with ts_validate_timestamps (detailed):'
SELECT
    group_col AS series_id,
    is_valid,
    n_expected,
    n_found,
    n_missing,
    missing_timestamps
FROM ts_validate_timestamps_by(
    'timestamp_test',
    series_id,
    ts,
    ['2024-01-01'::TIMESTAMP, '2024-01-02'::TIMESTAMP, '2024-01-03'::TIMESTAMP,
     '2024-01-04'::TIMESTAMP, '2024-01-05'::TIMESTAMP]
);

.print ''
.print 'Dataset-wide check with ts_validate_timestamps_summary (quick pass/fail):'
SELECT * FROM ts_validate_timestamps_summary_by(
    'timestamp_test',
    series_id,
    ts,
    ['2024-01-01'::TIMESTAMP, '2024-01-02'::TIMESTAMP, '2024-01-03'::TIMESTAMP,
     '2024-01-04'::TIMESTAMP, '2024-01-05'::TIMESTAMP]
);

.print ''
.print 'When to use: ts_validate_timestamps to find exactly which timestamps are missing,'
.print '             ts_validate_timestamps_summary for quick validation before bulk processing.'

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
DROP TABLE IF EXISTS stats_test;
DROP TABLE IF EXISTS computed_stats;
DROP TABLE IF EXISTS quality_test;
DROP TABLE IF EXISTS filter_test;
DROP TABLE IF EXISTS timestamp_test;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'DATA PREPARATION EXAMPLES COMPLETE'
.print '============================================================================='
