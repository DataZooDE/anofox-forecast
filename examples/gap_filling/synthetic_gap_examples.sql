-- =============================================================================
-- Gap Filling Examples - Synthetic Data
-- =============================================================================
-- This script demonstrates gap filling with the anofox-forecast extension
-- using 5 patterns from basic to advanced.
--
-- Run: ./build/release/duckdb < examples/gap_filling/synthetic_gap_examples.sql
-- =============================================================================

-- Load extension
LOAD anofox_forecast;

.print '============================================================================='
.print 'GAP FILLING EXAMPLES - Synthetic Data'
.print '============================================================================='

-- =============================================================================
-- SECTION 1: Fill Date Gaps (ts_fill_gaps_by)
-- =============================================================================
-- Use case: Insert missing dates in an irregular time series.

.print ''
.print '>>> SECTION 1: Fill Date Gaps (ts_fill_gaps_by)'
.print '-----------------------------------------------------------------------------'

-- Create data with missing dates
CREATE OR REPLACE TABLE gappy_data AS
SELECT * FROM (
    SELECT 'A' AS series_id, '2024-01-01'::DATE AS date, 100.0 AS value
    UNION ALL SELECT 'A', '2024-01-03'::DATE, 120.0  -- 01-02 missing
    UNION ALL SELECT 'A', '2024-01-04'::DATE, 130.0
    UNION ALL SELECT 'A', '2024-01-07'::DATE, 160.0  -- 01-05, 01-06 missing
    UNION ALL SELECT 'B', '2024-01-01'::DATE, 50.0
    UNION ALL SELECT 'B', '2024-01-02'::DATE, 55.0
    UNION ALL SELECT 'B', '2024-01-05'::DATE, 70.0   -- 01-03, 01-04 missing
);

.print 'Original data with gaps:'
SELECT series_id, LIST(date ORDER BY date) AS dates, LIST(value ORDER BY date) AS values
FROM gappy_data GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_fill_gaps (frequency=1 day):'
SELECT group_col AS series_id, date_col AS date, value_col AS value
FROM ts_fill_gaps_by('gappy_data', series_id, date, value, '1 day')
ORDER BY group_col, date_col;

-- =============================================================================
-- SECTION 2: Fill Forward to Target Date (ts_fill_forward_by)
-- =============================================================================
-- Use case: Extend series to a future date for forecasting.

.print ''
.print '>>> SECTION 2: Fill Forward to Target Date (ts_fill_forward_by)'
.print '-----------------------------------------------------------------------------'

-- Create series that ends early
CREATE OR REPLACE TABLE short_series AS
SELECT * FROM (
    SELECT 'A' AS series_id, '2024-01-01'::DATE AS date, 100.0 AS value
    UNION ALL SELECT 'A', '2024-01-02'::DATE, 110.0
    UNION ALL SELECT 'A', '2024-01-03'::DATE, 120.0
    UNION ALL SELECT 'B', '2024-01-01'::DATE, 50.0
    UNION ALL SELECT 'B', '2024-01-02'::DATE, 55.0
);

.print 'Original short series:'
SELECT series_id, MAX(date) AS last_date, COUNT(*) AS n_points
FROM short_series GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_fill_forward (extend to 2024-01-07):'
SELECT group_col AS series_id, date_col AS date, value_col AS value
FROM ts_fill_forward_by('short_series', series_id, date, value, '2024-01-07'::DATE, '1 day')
ORDER BY group_col, date_col;

-- =============================================================================
-- SECTION 3: Drop Gappy Series (ts_drop_gappy_by)
-- =============================================================================
-- Use case: Remove series with too many missing values.

.print ''
.print '>>> SECTION 3: Drop Gappy Series (ts_drop_gappy_by)'
.print '-----------------------------------------------------------------------------'

-- Create series with different gap ratios
CREATE OR REPLACE TABLE quality_test AS
SELECT * FROM (
    -- Series A: 1/5 = 20% gaps (good quality)
    SELECT 'A' AS series_id, '2024-01-01'::DATE AS date, 100.0 AS value
    UNION ALL SELECT 'A', '2024-01-02'::DATE, NULL
    UNION ALL SELECT 'A', '2024-01-03'::DATE, 120.0
    UNION ALL SELECT 'A', '2024-01-04'::DATE, 130.0
    UNION ALL SELECT 'A', '2024-01-05'::DATE, 140.0
    UNION ALL
    -- Series B: 3/5 = 60% gaps (poor quality)
    SELECT 'B' AS series_id, '2024-01-01'::DATE AS date, NULL
    UNION ALL SELECT 'B', '2024-01-02'::DATE, NULL
    UNION ALL SELECT 'B', '2024-01-03'::DATE, 50.0
    UNION ALL SELECT 'B', '2024-01-04'::DATE, NULL
    UNION ALL SELECT 'B', '2024-01-05'::DATE, 70.0
    UNION ALL
    -- Series C: 0/5 = 0% gaps (complete)
    SELECT 'C' AS series_id, '2024-01-01'::DATE AS date, 200.0
    UNION ALL SELECT 'C', '2024-01-02'::DATE, 210.0
    UNION ALL SELECT 'C', '2024-01-03'::DATE, 220.0
    UNION ALL SELECT 'C', '2024-01-04'::DATE, 230.0
    UNION ALL SELECT 'C', '2024-01-05'::DATE, 240.0
);

.print 'Original data gap ratios:'
SELECT
    series_id,
    COUNT(*) AS n_total,
    SUM(CASE WHEN value IS NULL THEN 1 ELSE 0 END) AS n_nulls,
    ROUND(SUM(CASE WHEN value IS NULL THEN 1.0 ELSE 0.0 END) / COUNT(*), 2) AS gap_ratio
FROM quality_test GROUP BY series_id ORDER BY series_id;

.print ''
.print 'After ts_drop_gappy (max_gap_ratio=0.3):';
SELECT series_id, COUNT(*) AS n_points
FROM ts_drop_gappy_by('quality_test', series_id, value, 0.3)
GROUP BY series_id ORDER BY series_id;

-- =============================================================================
-- SECTION 4: Fill Unknown Future Values (ts_fill_unknown_by)
-- =============================================================================
-- Use case: Handle future feature values in cross-validation.

.print ''
.print '>>> SECTION 4: Fill Unknown Future Values (ts_fill_unknown_by)'
.print '-----------------------------------------------------------------------------'

-- Create time series with features
CREATE OR REPLACE TABLE feature_data AS
SELECT * FROM (
    SELECT 'A' AS series_id, '2024-01-01'::TIMESTAMP AS date, 100.0 AS feature
    UNION ALL SELECT 'A', '2024-01-02'::TIMESTAMP, 110.0
    UNION ALL SELECT 'A', '2024-01-03'::TIMESTAMP, 120.0
    UNION ALL SELECT 'A', '2024-01-04'::TIMESTAMP, 130.0
    UNION ALL SELECT 'A', '2024-01-05'::TIMESTAMP, 140.0
);

.print 'Original feature data:'
SELECT * FROM feature_data ORDER BY series_id, date;

-- Simulate cross-validation: cutoff at 2024-01-03, fill unknown with last known value
.print ''
.print 'After ts_fill_unknown (cutoff=2024-01-03, strategy=last_value):'
SELECT group_col AS series_id, date_col AS date, value_col AS feature
FROM ts_fill_unknown_by('feature_data', series_id, date, feature, '2024-01-03'::TIMESTAMP, {'strategy': 'last_value'})
ORDER BY group_col, date_col;

-- Fill with NULL instead
.print ''
.print 'After ts_fill_unknown (cutoff=2024-01-03, strategy=null):'
SELECT group_col AS series_id, date_col AS date, value_col AS feature
FROM ts_fill_unknown_by('feature_data', series_id, date, feature, '2024-01-03'::TIMESTAMP, {'strategy': 'null'})
ORDER BY group_col, date_col;

-- Fill with default value
.print ''
.print 'After ts_fill_unknown (cutoff=2024-01-03, strategy=default, fill_value=0):'
SELECT group_col AS series_id, date_col AS date, value_col AS feature
FROM ts_fill_unknown_by('feature_data', series_id, date, feature, '2024-01-03'::TIMESTAMP, {'strategy': 'default', 'fill_value': '0'})
ORDER BY group_col, date_col;

-- =============================================================================
-- SECTION 5: Different Frequency Formats
-- =============================================================================
-- Use case: Support multiple frequency notation styles.

.print ''
.print '>>> SECTION 5: Different Frequency Formats'
.print '-----------------------------------------------------------------------------'

-- Create hourly data with gaps
CREATE OR REPLACE TABLE hourly_data AS
SELECT * FROM (
    SELECT 'A' AS series_id, '2024-01-01 00:00:00'::TIMESTAMP AS ts, 100.0 AS value
    UNION ALL SELECT 'A', '2024-01-01 02:00:00'::TIMESTAMP, 120.0  -- 01:00 missing
    UNION ALL SELECT 'A', '2024-01-01 03:00:00'::TIMESTAMP, 130.0
);

.print 'Hourly data with gaps:'
SELECT series_id, ts, value FROM hourly_data ORDER BY series_id, ts;

.print ''
.print 'Fill gaps with DuckDB interval format (1 hour):'
SELECT group_col, date_col, value_col
FROM ts_fill_gaps_by('hourly_data', series_id, ts, value, '1 hour')
ORDER BY group_col, date_col;

.print ''
.print 'Fill gaps with Polars-style format (1h):'
SELECT group_col, date_col, value_col
FROM ts_fill_gaps_by('hourly_data', series_id, ts, value, '1h')
ORDER BY group_col, date_col;

-- =============================================================================
-- CLEANUP
-- =============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS gappy_data;
DROP TABLE IF EXISTS short_series;
DROP TABLE IF EXISTS quality_test;
DROP TABLE IF EXISTS feature_data;
DROP TABLE IF EXISTS hourly_data;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'GAP FILLING EXAMPLES COMPLETE'
.print '============================================================================='
