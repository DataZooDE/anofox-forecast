-- ============================================================================
-- Changepoint Detection Examples - Testing with Known Breaks
-- ============================================================================
-- This file demonstrates that changepoint detection correctly identifies
-- structural breaks in synthetic time series with known changepoints.
--
-- Run with:
--   ./build/release/duckdb < examples/changepoint_detection/synthetic_changepoint_examples.sql
-- ============================================================================

LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- ============================================================================
-- SECTION 1: Basic Changepoint Detection Tests
-- ============================================================================

.print '============================================================'
.print 'SECTION 1: Basic Changepoint Detection Tests'
.print '============================================================'

-- Test 1: Mean shift at index 50
WITH mean_shift AS (
    SELECT list(CASE WHEN i < 50 THEN 10.0 + (random()-0.5)*2 ELSE 50.0 + (random()-0.5)*2 END) AS v
    FROM generate_series(0, 99) AS t(i)
)
SELECT 'Mean Shift (expect ~50)' AS test, (_ts_detect_changepoints_bocpd(v, 100.0, false)).changepoint_indices AS detected FROM mean_shift;

-- Test 2: Step function (perfect break)
SELECT 'Step Function (expect 5)' AS test, (_ts_detect_changepoints_bocpd([1,1,1,1,1,10,10,10,10,10]::DOUBLE[], 100.0, false)).changepoint_indices AS detected;

-- Test 3: Multiple changepoints at 33 and 67
WITH multi AS (
    SELECT list(CASE WHEN i < 33 THEN 10.0 WHEN i < 67 THEN 30.0 ELSE 50.0 END + (random()-0.5)*2) AS v
    FROM generate_series(0, 99) AS t(i)
)
SELECT 'Multi CP (expect ~33, ~67)' AS test, (_ts_detect_changepoints_bocpd(v, 50.0, false)).changepoint_indices AS detected FROM multi;

-- Test 4: Stationary series (expect no changepoints)
WITH stationary AS (
    SELECT list(10.0 + (random()-0.5)*2) AS v FROM generate_series(0, 99) AS t(i)
)
SELECT 'Stationary (expect empty)' AS test, (_ts_detect_changepoints_bocpd(v, 100.0, false)).changepoint_indices AS detected FROM stationary;

-- ============================================================================
-- SECTION 2: Scale Tests
-- ============================================================================

.print ''
.print '============================================================'
.print 'SECTION 2: Scale Tests'
.print '============================================================'

-- Test with 10K points
WITH series_10k AS (
    SELECT list(CASE WHEN i < 5000 THEN 10.0 ELSE 50.0 END + (random()-0.5)*2) AS v
    FROM generate_series(0, 9999) AS t(i)
)
SELECT '10K points (expect ~5000)' AS test, (_ts_detect_changepoints_bocpd(v, 1000.0, false)).changepoint_indices AS detected FROM series_10k;

-- Test with 100K points
WITH series_100k AS (
    SELECT list(CASE WHEN i < 50000 THEN 10.0 ELSE 50.0 END + (random()-0.5)*2) AS v
    FROM generate_series(0, 99999) AS t(i)
)
SELECT '100K points (expect ~50000)' AS test, (_ts_detect_changepoints_bocpd(v, 10000.0, false)).changepoint_indices AS detected FROM series_100k;

-- ============================================================================
-- SECTION 3: Table Macro Tests
-- ============================================================================

.print ''
.print '============================================================'
.print 'SECTION 3: Table Macro Tests'
.print '============================================================'

-- Create test table with multiple series
CREATE OR REPLACE TABLE test_series AS
WITH series_a AS (
    SELECT
        'series_a' AS series_id,
        DATE '2020-01-01' + INTERVAL (i) DAY AS ds,
        CASE WHEN i < 50 THEN 10.0 ELSE 50.0 END + (random()-0.5)*2 AS value
    FROM generate_series(0, 99) AS t(i)
),
series_b AS (
    SELECT
        'series_b' AS series_id,
        DATE '2020-01-01' + INTERVAL (i) DAY AS ds,
        CASE WHEN i < 30 THEN 5.0 WHEN i < 70 THEN 25.0 ELSE 15.0 END + (random()-0.5)*1 AS value
    FROM generate_series(0, 99) AS t(i)
)
SELECT * FROM series_a
UNION ALL
SELECT * FROM series_b;

-- Test ts_detect_changepoints_by with multiple series
SELECT
    id,
    (changepoints).changepoint_indices AS detected_indices
FROM ts_detect_changepoints_by('test_series', series_id, ds, value, MAP{});

-- ============================================================================
-- SECTION 4: Aggregate Function Test
-- ============================================================================

.print ''
.print '============================================================'
.print 'SECTION 4: Aggregate Function Test'
.print '============================================================'

SELECT
    series_id,
    list_filter(
        ts_detect_changepoints_agg(ds, value, MAP{}),
        x -> x.is_changepoint
    ) AS changepoint_rows
FROM test_series
GROUP BY series_id;

-- ============================================================================
-- SECTION 5: Edge Cases
-- ============================================================================

.print ''
.print '============================================================'
.print 'SECTION 5: Edge Cases'
.print '============================================================'

-- NaN handling (returns no changepoints, doesn't crash)
SELECT 'NaN values' AS test, (_ts_detect_changepoints_bocpd([1.0, 2.0, 'NaN'::DOUBLE, 4.0, 5.0], 10.0, false)).changepoint_indices AS result;

-- Inf handling (returns no changepoints, doesn't crash)
SELECT 'Inf values' AS test, (_ts_detect_changepoints_bocpd([1.0, 2.0, 'Inf'::DOUBLE, 4.0, 5.0], 10.0, false)).changepoint_indices AS result;

-- Constant series (no changepoints expected)
SELECT 'Constant series' AS test, (_ts_detect_changepoints_bocpd([5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0, 5.0], 10.0, false)).changepoint_indices AS result;

-- Single element (returns NULL)
SELECT 'Single element' AS test, _ts_detect_changepoints_bocpd([5.0], 10.0, false) AS result;

-- NOTE: Empty arrays crash - this is a known bug (Issue #82)
-- SELECT 'Empty array' AS test, _ts_detect_changepoints_bocpd([]::DOUBLE[], 10.0, false);
-- CRASHES with: INTERNAL Error: Operation requires a flat vector but a non-flat vector was encountered

.print ''
.print '============================================================'
.print 'All tests completed!'
.print '============================================================'
