-- Investigation: Segfault in ts_backtest_auto_by on M5 dataset with limited memory (#105)
-- This file profiles memory usage of ts_backtest_auto_by with varying dataset sizes

LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- ============================================
-- Configuration
-- ============================================

-- Set memory limit to simulate constrained environment
-- Adjust this to test different memory scenarios
SET memory_limit = '2GB';
SET temp_directory = '/tmp/duckdb_temp';

-- Show current settings
SELECT current_setting('memory_limit') AS memory_limit;

-- ============================================
-- Create synthetic M5-like dataset
-- M5 actual: 30,490 series × 1,941 points = ~59M rows
-- We'll test with smaller subsets first
-- ============================================

-- Parameter: number of series to test
-- Start small and increase: 100, 500, 1000, 5000
.parameter n_series 100

-- Create test data: n_series × 200 points each
CREATE OR REPLACE TABLE test_data AS
SELECT
    'series_' || LPAD(s::VARCHAR, 5, '0') AS item_id,
    '2020-01-01'::DATE + (d * INTERVAL '1 day') AS ds,
    -- Simulated demand with trend + seasonality + noise
    100.0 + (s % 50) + (d * 0.1) + (10 * SIN(d * 3.14159 / 7)) + (RANDOM() * 20) AS y
FROM generate_series(1, $n_series) AS t(s)
CROSS JOIN generate_series(0, 199) AS u(d);

-- Show dataset size
SELECT
    'Dataset created' AS status,
    COUNT(DISTINCT item_id) AS n_series,
    COUNT(*) AS total_rows,
    MIN(ds) AS min_date,
    MAX(ds) AS max_date
FROM test_data;

-- ============================================
-- Test 1: Profile individual CTEs
-- Run each CTE separately to identify memory hotspot
-- ============================================

PRAGMA enable_progress_bar;
PRAGMA enable_profiling='query_tree';

-- Test 1a: Just the source data grouping
.timer on
SELECT 'CTE: src - source data grouping' AS test;
CREATE OR REPLACE TABLE profile_src AS
SELECT
    item_id AS _grp,
    date_trunc('second', ds::TIMESTAMP) AS _dt,
    y::DOUBLE AS _target
FROM test_data;
SELECT COUNT(*) AS src_rows FROM profile_src;

-- Test 1b: Date bounds (should be tiny)
SELECT 'CTE: date_bounds' AS test;
SELECT
    MIN(_dt) AS _min_dt,
    MAX(_dt) AS _max_dt,
    COUNT(DISTINCT _dt) AS _n_dates
FROM profile_src;

-- Test 1c: Fold bounds generation (should be small)
SELECT 'CTE: fold_bounds with 5 folds' AS test;
WITH _params AS (
    SELECT
        'Naive' AS _method,
        'expanding' AS _window_type,
        1 AS _min_train_size,
        0 AS _gap,
        0 AS _embargo,
        NULL::BIGINT AS _init_train_size,
        NULL::BIGINT AS _skip_length_param,
        FALSE AS _clip_horizon
),
_freq AS (SELECT INTERVAL '1 day' AS _interval),
date_bounds AS (
    SELECT MIN(_dt) AS _min_dt, MAX(_dt) AS _max_dt, COUNT(DISTINCT _dt) AS _n_dates
    FROM profile_src
),
_computed AS (
    SELECT
        _min_dt, _max_dt, _n_dates,
        COALESCE((SELECT _init_train_size FROM _params), GREATEST((_n_dates / 2)::BIGINT, 1)) AS _init_size,
        COALESCE((SELECT _skip_length_param FROM _params), 7::BIGINT) AS _skip_length,
        (SELECT _clip_horizon FROM _params) AS _clip_horizon,
        (SELECT _interval FROM _freq) AS _interval
    FROM date_bounds
),
fold_end_times AS (
    SELECT _min_dt + (_init_size * _interval) + ((generate_series - 1) * _skip_length * _interval) AS train_end
    FROM _computed, generate_series(1, 5)
    WHERE _min_dt + (_init_size * _interval) + ((generate_series - 1) * _skip_length * _interval) + (7 * _interval) <= _max_dt
),
fold_bounds AS (
    SELECT
        ROW_NUMBER() OVER (ORDER BY train_end) AS fold_id,
        (SELECT _min_dt FROM date_bounds) AS train_start,
        train_end,
        train_end + (1 * (SELECT _interval FROM _freq)) AS test_start,
        train_end + (7 * (SELECT _interval FROM _freq)) AS test_end
    FROM fold_end_times
)
SELECT * FROM fold_bounds;

-- Test 1d: cv_splits - THE SUSPECTED MEMORY HOTSPOT
-- This does CROSS JOIN between src (n_series × n_dates) and fold_bounds (n_folds)
SELECT 'CTE: cv_splits - CROSS JOIN expansion (SUSPECTED HOTSPOT)' AS test;
WITH fold_bounds AS (
    SELECT 1 AS fold_id, '2020-01-01'::TIMESTAMP AS train_start, '2020-04-01'::TIMESTAMP AS train_end,
           '2020-04-02'::TIMESTAMP AS test_start, '2020-04-08'::TIMESTAMP AS test_end
    UNION ALL
    SELECT 2, '2020-01-01', '2020-04-08', '2020-04-09', '2020-04-15'
    UNION ALL
    SELECT 3, '2020-01-01', '2020-04-15', '2020-04-16', '2020-04-22'
    UNION ALL
    SELECT 4, '2020-01-01', '2020-04-22', '2020-04-23', '2020-04-29'
    UNION ALL
    SELECT 5, '2020-01-01', '2020-04-29', '2020-04-30', '2020-05-06'
)
SELECT
    'cv_splits row expansion' AS metric,
    (SELECT COUNT(*) FROM profile_src) AS src_rows,
    (SELECT COUNT(*) FROM fold_bounds) AS n_folds,
    (SELECT COUNT(*) FROM profile_src) * (SELECT COUNT(*) FROM fold_bounds) AS theoretical_max_rows
;

-- Actually create cv_splits to measure memory
CREATE OR REPLACE TABLE profile_cv_splits AS
WITH fold_bounds AS (
    SELECT 1 AS fold_id, '2020-01-01'::TIMESTAMP AS train_start, '2020-04-01'::TIMESTAMP AS train_end,
           '2020-04-02'::TIMESTAMP AS test_start, '2020-04-08'::TIMESTAMP AS test_end
    UNION ALL
    SELECT 2, '2020-01-01', '2020-04-08', '2020-04-09', '2020-04-15'
    UNION ALL
    SELECT 3, '2020-01-01', '2020-04-15', '2020-04-16', '2020-04-22'
    UNION ALL
    SELECT 4, '2020-01-01', '2020-04-22', '2020-04-23', '2020-04-29'
    UNION ALL
    SELECT 5, '2020-01-01', '2020-04-29', '2020-04-30', '2020-05-06'
)
SELECT
    s._grp AS _grp,
    s._dt AS _dt,
    s._target AS _target,
    fb.fold_id::BIGINT AS fold_id,
    CASE
        WHEN s._dt >= fb.train_start AND s._dt <= fb.train_end THEN 'train'
        WHEN s._dt >= fb.test_start AND s._dt <= fb.test_end THEN 'test'
        ELSE NULL
    END AS split
FROM profile_src s
CROSS JOIN fold_bounds fb
WHERE (s._dt >= fb.train_start AND s._dt <= fb.train_end)
   OR (s._dt >= fb.test_start AND s._dt <= fb.test_end);

SELECT
    'cv_splits actual rows' AS metric,
    COUNT(*) AS actual_rows,
    COUNT(*) / (SELECT COUNT(DISTINCT _grp) FROM profile_cv_splits) AS rows_per_series
FROM profile_cv_splits;

-- Test 1e: forecast_data - LIST aggregation
SELECT 'CTE: forecast_data - LIST aggregation per group per fold' AS test;
CREATE OR REPLACE TABLE profile_cv_train AS
SELECT fold_id, _grp, _dt, _target FROM profile_cv_splits WHERE split = 'train';

SELECT
    'cv_train stats' AS metric,
    COUNT(*) AS total_rows,
    COUNT(DISTINCT fold_id) AS n_folds,
    COUNT(DISTINCT _grp) AS n_groups,
    COUNT(*) / COUNT(DISTINCT fold_id) / COUNT(DISTINCT _grp) AS avg_train_length
FROM profile_cv_train;

-- ============================================
-- Test 2: Full ts_backtest_auto_by call
-- Monitor memory usage
-- ============================================

SELECT 'FULL TEST: ts_backtest_auto_by' AS test;
SELECT
    'Parameters' AS info,
    $n_series AS n_series,
    200 AS points_per_series,
    5 AS folds,
    7 AS horizon;

-- This is where the segfault happens with large datasets
-- Comment out if it crashes
CREATE OR REPLACE TABLE backtest_result AS
SELECT * FROM ts_backtest_auto_by(
    'test_data',
    item_id,
    ds,
    y,
    7,      -- horizon
    5,      -- folds
    '1d',   -- frequency
    MAP{'method': 'Naive'}
);

SELECT
    'Backtest completed' AS status,
    COUNT(*) AS total_rows,
    COUNT(DISTINCT fold_id) AS n_folds,
    COUNT(DISTINCT group_col) AS n_groups
FROM backtest_result;

-- ============================================
-- Test 3: Memory limit test
-- Progressively reduce memory to find threshold
-- ============================================

-- Clean up
DROP TABLE IF EXISTS profile_src;
DROP TABLE IF EXISTS profile_cv_splits;
DROP TABLE IF EXISTS profile_cv_train;
DROP TABLE IF EXISTS backtest_result;
DROP TABLE IF EXISTS test_data;

SELECT 'Investigation complete' AS status;
