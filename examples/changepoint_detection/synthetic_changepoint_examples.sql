-- =============================================================================
-- Changepoint Detection Examples - Synthetic Data
-- =============================================================================
-- This script demonstrates changepoint detection with the anofox-forecast
-- extension using 5 patterns from basic to advanced.
--
-- Run: ./build/release/duckdb < examples/changepoint_detection/synthetic_changepoint_examples.sql
-- =============================================================================

-- Load extension
LOAD anofox_forecast;

.print '============================================================================='
.print 'CHANGEPOINT DETECTION EXAMPLES - Synthetic Data'
.print '============================================================================='

-- =============================================================================
-- SECTION 1: Quick Start (Table Macro)
-- =============================================================================
-- Use case: Detect changepoints in a time series using the table macro.

.print ''
.print '>>> SECTION 1: Quick Start (Table Macro)'
.print '-----------------------------------------------------------------------------'

-- Create a table with level shift at index 20
CREATE OR REPLACE TABLE level_shift_data AS
SELECT
    '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
    CASE WHEN i < 20 THEN 100.0 + (RANDOM() - 0.5) * 5
         ELSE 150.0 + (RANDOM() - 0.5) * 5 END AS value
FROM generate_series(0, 39) AS t(i);

.print 'Input data summary:'
SELECT
    COUNT(*) AS n_points,
    ROUND(AVG(CASE WHEN date < '2024-01-21' THEN value END), 2) AS avg_before,
    ROUND(AVG(CASE WHEN date >= '2024-01-21' THEN value END), 2) AS avg_after
FROM level_shift_data;

-- Use table macro for changepoint detection
.print ''
.print 'Detect changepoints using table macro:'
SELECT * FROM ts_detect_changepoints('level_shift_data', date, value, MAP{})
ORDER BY date_col
LIMIT 10;

-- Show which points are detected as changepoints
.print ''
.print 'Points flagged as changepoints:'
SELECT date_col, value_col, is_changepoint, ROUND(changepoint_probability, 4) AS prob
FROM ts_detect_changepoints('level_shift_data', date, value, MAP{})
WHERE is_changepoint = true
ORDER BY date_col;

-- =============================================================================
-- SECTION 2: BOCPD Scalar Function
-- =============================================================================
-- Use case: Use the scalar BOCPD function on array data.

.print ''
.print '>>> SECTION 2: BOCPD Scalar Function'
.print '-----------------------------------------------------------------------------'

-- Simple level shift detection
.print 'BOCPD on array with level shift:'
SELECT
    _ts_detect_changepoints_bocpd(
        [1.0, 1.0, 1.0, 1.0, 10.0, 10.0, 10.0, 10.0],
        250.0,  -- hazard_lambda
        true    -- include_probabilities
    ) AS result;

-- Extract changepoint indices
.print ''
.print 'Extract changepoint indices:'
SELECT
    (_ts_detect_changepoints_bocpd(
        [1.0, 1.0, 1.0, 1.0, 10.0, 10.0, 10.0, 10.0],
        250.0, true
    )).changepoint_indices AS indices;

-- =============================================================================
-- SECTION 3: Parameter Tuning (hazard_lambda)
-- =============================================================================
-- Use case: Adjust sensitivity with hazard_lambda parameter.

.print ''
.print '>>> SECTION 3: Parameter Tuning (hazard_lambda)'
.print '-----------------------------------------------------------------------------'

-- Create data with multiple regimes
CREATE OR REPLACE TABLE multi_regime_data AS
SELECT
    '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
    CASE
        WHEN i < 20 THEN 50.0 + (RANDOM() - 0.5) * 5
        WHEN i < 40 THEN 100.0 + (RANDOM() - 0.5) * 5
        ELSE 75.0 + (RANDOM() - 0.5) * 5
    END AS value
FROM generate_series(0, 59) AS t(i);

.print 'Data with 3 regimes (expected changepoints around index 20 and 40):'
SELECT
    CASE
        WHEN ts < '2024-01-21' THEN 'Regime 1'
        WHEN ts < '2024-02-10' THEN 'Regime 2'
        ELSE 'Regime 3'
    END AS regime,
    COUNT(*) AS n_points,
    ROUND(AVG(value), 2) AS avg_value
FROM multi_regime_data
GROUP BY 1 ORDER BY 1;

-- Default hazard_lambda (250.0)
.print ''
.print 'With default hazard_lambda (250.0):'
SELECT date_col, ROUND(changepoint_probability, 4) AS prob
FROM ts_detect_changepoints('multi_regime_data', ts, value, MAP{})
WHERE changepoint_probability > 0.1
ORDER BY date_col;

-- Lower hazard_lambda (more sensitive)
.print ''
.print 'With hazard_lambda=100 (more sensitive):'
SELECT date_col, ROUND(changepoint_probability, 4) AS prob
FROM ts_detect_changepoints('multi_regime_data', ts, value, MAP{'hazard_lambda': '100.0'})
WHERE changepoint_probability > 0.1
ORDER BY date_col;

-- =============================================================================
-- SECTION 4: Multi-Series Detection (Grouped)
-- =============================================================================
-- Use case: Detect changepoints across multiple time series.

.print ''
.print '>>> SECTION 4: Multi-Series Detection (Grouped)'
.print '-----------------------------------------------------------------------------'

-- Create multi-series data with different changepoint locations
CREATE OR REPLACE TABLE multi_series_cp AS
SELECT * FROM (
    -- Series A: changepoint at day 15
    SELECT
        'Series_A' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        CASE WHEN i < 15 THEN 50.0 + (RANDOM() - 0.5) * 5
             ELSE 80.0 + (RANDOM() - 0.5) * 5 END AS value
    FROM generate_series(0, 29) AS t(i)
    UNION ALL
    -- Series B: changepoint at day 20
    SELECT
        'Series_B' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        CASE WHEN i < 20 THEN 100.0 + (RANDOM() - 0.5) * 8
             ELSE 60.0 + (RANDOM() - 0.5) * 8 END AS value
    FROM generate_series(0, 29) AS t(i)
    UNION ALL
    -- Series C: no changepoint (stable)
    SELECT
        'Series_C' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        75.0 + (RANDOM() - 0.5) * 5 AS value
    FROM generate_series(0, 29) AS t(i)
);

.print 'Series summary:'
SELECT series_id, COUNT(*) AS n_points, ROUND(AVG(value), 2) AS avg_value
FROM multi_series_cp GROUP BY series_id ORDER BY series_id;

-- Use ts_detect_changepoints_by table macro
.print ''
.print 'Using ts_detect_changepoints_by (grouped detection):'
SELECT
    id,
    (changepoints).changepoint_indices AS indices,
    length((changepoints).is_changepoint) AS n_points
FROM ts_detect_changepoints_by('multi_series_cp', series_id, date, value, MAP{})
ORDER BY id;

-- Show probabilities for each series
.print ''
.print 'Changepoint probabilities per series (first 5 values):'
SELECT
    id,
    (changepoints).changepoint_probability[1:5] AS first_5_probs
FROM ts_detect_changepoints_by('multi_series_cp', series_id, date, value, MAP{})
ORDER BY id;

-- =============================================================================
-- SECTION 5: Aggregate Function
-- =============================================================================
-- Use case: Use aggregate function for custom grouping.

.print ''
.print '>>> SECTION 5: Aggregate Function'
.print '-----------------------------------------------------------------------------'

-- Use aggregate function per series
.print 'Using ts_detect_changepoints_agg:'
SELECT
    series_id,
    length(ts_detect_changepoints_agg(date, value, MAP{})) AS n_points
FROM multi_series_cp
GROUP BY series_id
ORDER BY series_id;

-- Extract changepoint info from aggregate result
.print ''
.print 'Aggregate result structure:'
SELECT
    series_id,
    ts_detect_changepoints_agg(date, value, MAP{}) AS result
FROM multi_series_cp
WHERE series_id = 'Series_A'
GROUP BY series_id;

-- =============================================================================
-- CLEANUP
-- =============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS level_shift_data;
DROP TABLE IF EXISTS multi_regime_data;
DROP TABLE IF EXISTS multi_series_cp;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'CHANGEPOINT DETECTION EXAMPLES COMPLETE'
.print '============================================================================='
