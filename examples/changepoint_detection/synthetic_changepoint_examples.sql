-- ============================================================================
-- Changepoint Detection Examples - Synthetic Data
-- ============================================================================
-- This script demonstrates changepoint detection with the anofox-forecast
-- extension using the ts_detect_changepoints_by table macro.
--
-- Run: ./build/release/duckdb < examples/changepoint_detection/synthetic_changepoint_examples.sql
-- ============================================================================

-- Load extension
LOAD anofox_forecast;
INSTALL json;
LOAD json;

.print '============================================================================='
.print 'CHANGEPOINT DETECTION EXAMPLES - Using ts_detect_changepoints_by'
.print '============================================================================='

-- ============================================================================
-- SECTION 1: Basic Changepoint Detection for Multiple Series
-- ============================================================================
-- Use ts_detect_changepoints_by to detect structural breaks across grouped series.

.print ''
.print '>>> SECTION 1: Basic Changepoint Detection'
.print '-----------------------------------------------------------------------------'

-- Create multi-series data with different changepoint patterns
CREATE OR REPLACE TABLE multi_series AS
SELECT * FROM (
    -- Series A: Single mean shift at day 50
    SELECT
        'series_A' AS series_id,
        DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
        CASE WHEN i < 50 THEN 10.0 ELSE 50.0 END + (RANDOM() - 0.5) * 4 AS value
    FROM generate_series(0, 99) AS t(i)
    UNION ALL
    -- Series B: Two changepoints at day 33 and 67
    SELECT
        'series_B' AS series_id,
        DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
        CASE
            WHEN i < 33 THEN 10.0
            WHEN i < 67 THEN 30.0
            ELSE 50.0
        END + (RANDOM() - 0.5) * 4 AS value
    FROM generate_series(0, 99) AS t(i)
    UNION ALL
    -- Series C: No changepoints (stationary)
    SELECT
        'series_C' AS series_id,
        DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
        25.0 + (RANDOM() - 0.5) * 4 AS value
    FROM generate_series(0, 99) AS t(i)
);

.print 'Multi-series data summary:'
SELECT series_id, COUNT(*) AS n_rows, ROUND(AVG(value), 2) AS avg_value
FROM multi_series GROUP BY series_id ORDER BY series_id;

-- 1.1: Basic changepoint detection (default parameters)
.print ''
.print 'Section 1.1: Basic Changepoint Detection'

SELECT
    id,
    (changepoints).changepoint_indices AS detected_indices,
    list_count((changepoints).changepoint_indices) AS n_changepoints
FROM ts_detect_changepoints_by('multi_series', series_id, ds, value, MAP{});

-- ============================================================================
-- SECTION 2: Tuning Detection Sensitivity
-- ============================================================================

.print ''
.print '>>> SECTION 2: Tuning Detection Sensitivity (hazard_lambda)'
.print '-----------------------------------------------------------------------------'

-- 2.1: Conservative detection (higher hazard_lambda = fewer changepoints)
.print 'Section 2.1: Conservative Detection (hazard_lambda=500)'

SELECT
    id,
    (changepoints).changepoint_indices AS detected_indices
FROM ts_detect_changepoints_by('multi_series', series_id, ds, value, MAP{'hazard_lambda': '500'});

-- 2.2: Sensitive detection (lower hazard_lambda = more changepoints)
.print ''
.print 'Section 2.2: Sensitive Detection (hazard_lambda=50)'

SELECT
    id,
    (changepoints).changepoint_indices AS detected_indices
FROM ts_detect_changepoints_by('multi_series', series_id, ds, value, MAP{'hazard_lambda': '50'});

-- ============================================================================
-- SECTION 3: Accessing Changepoint Details
-- ============================================================================

.print ''
.print '>>> SECTION 3: Accessing Changepoint Details'
.print '-----------------------------------------------------------------------------'

-- 3.1: Get changepoint probabilities
.print 'Section 3.1: Changepoint Probabilities (include_probabilities=true)'

SELECT
    id,
    (changepoints).changepoint_indices AS indices,
    list_count((changepoints).changepoint_probability) AS n_probs,
    ROUND(list_max((changepoints).changepoint_probability), 4) AS max_probability
FROM ts_detect_changepoints_by('multi_series', series_id, ds, value, MAP{'include_probabilities': 'true'});

-- ============================================================================
-- SECTION 4: Real-World Scenarios
-- ============================================================================

.print ''
.print '>>> SECTION 4: Real-World Scenarios'
.print '-----------------------------------------------------------------------------'

-- Create retail demand data with promotional changepoints
CREATE OR REPLACE TABLE retail_demand AS
SELECT * FROM (
    -- Store A: Promotion effect starting day 30
    SELECT
        'Store_A' AS store_id,
        DATE '2024-01-01' + INTERVAL (i) DAY AS date,
        CASE
            WHEN i < 30 THEN 100.0
            WHEN i < 45 THEN 180.0  -- promotion period
            ELSE 110.0  -- post-promotion (slightly higher baseline)
        END + (RANDOM() - 0.5) * 15 AS sales
    FROM generate_series(0, 89) AS t(i)
    UNION ALL
    -- Store B: Gradual decline starting day 40
    SELECT
        'Store_B' AS store_id,
        DATE '2024-01-01' + INTERVAL (i) DAY AS date,
        CASE
            WHEN i < 40 THEN 200.0
            ELSE 200.0 - (i - 40) * 1.5
        END + (RANDOM() - 0.5) * 20 AS sales
    FROM generate_series(0, 89) AS t(i)
    UNION ALL
    -- Store C: Stable (no changepoints)
    SELECT
        'Store_C' AS store_id,
        DATE '2024-01-01' + INTERVAL (i) DAY AS date,
        150.0 + (RANDOM() - 0.5) * 25 AS sales
    FROM generate_series(0, 89) AS t(i)
);

.print 'Section 4.1: Retail Demand Changepoint Detection'

SELECT
    id AS store,
    (changepoints).changepoint_indices AS detected_changepoints,
    list_count((changepoints).changepoint_indices) AS n_changepoints,
    CASE
        WHEN list_count((changepoints).changepoint_indices) = 0 THEN 'Stable demand'
        WHEN list_count((changepoints).changepoint_indices) = 1 THEN 'Single shift detected'
        ELSE 'Multiple shifts detected'
    END AS pattern_description
FROM ts_detect_changepoints_by('retail_demand', store_id, date, sales, MAP{'hazard_lambda': '30'});

-- ============================================================================
-- SECTION 5: Sensor Data Anomaly Detection
-- ============================================================================

.print ''
.print '>>> SECTION 5: Sensor Data Anomaly Detection'
.print '-----------------------------------------------------------------------------'

-- Create sensor data with equipment failure
CREATE OR REPLACE TABLE sensor_data AS
SELECT * FROM (
    -- Sensor A: Equipment failure at reading 60
    SELECT
        'Sensor_A' AS sensor_id,
        '2024-01-01 00:00:00'::TIMESTAMP + INTERVAL (i) HOUR AS timestamp,
        CASE
            WHEN i < 60 THEN 50.0 + 5.0 * SIN(2 * PI() * i / 24)  -- normal operation
            WHEN i < 75 THEN 80.0 + (RANDOM() - 0.5) * 30  -- erratic readings
            ELSE 0.0  -- sensor failure
        END + (RANDOM() - 0.5) * 3 AS reading
    FROM generate_series(0, 119) AS t(i)
    UNION ALL
    -- Sensor B: Normal operation
    SELECT
        'Sensor_B' AS sensor_id,
        '2024-01-01 00:00:00'::TIMESTAMP + INTERVAL (i) HOUR AS timestamp,
        50.0 + 5.0 * SIN(2 * PI() * i / 24) + (RANDOM() - 0.5) * 3 AS reading
    FROM generate_series(0, 119) AS t(i)
);

.print 'Section 5.1: Sensor Anomaly Detection'

SELECT
    id AS sensor,
    (changepoints).changepoint_indices AS anomaly_indices,
    CASE
        WHEN list_count((changepoints).changepoint_indices) > 0 THEN 'ALERT: Anomalies detected'
        ELSE 'Normal operation'
    END AS status
FROM ts_detect_changepoints_by('sensor_data', sensor_id, timestamp, reading, MAP{'hazard_lambda': '20'});

-- ============================================================================
-- SECTION 6: Comparing Detection Across Series
-- ============================================================================

.print ''
.print '>>> SECTION 6: Comparing Detection Across Series'
.print '-----------------------------------------------------------------------------'

.print 'Section 6.1: Summary of Changepoints by Series'

WITH detection_results AS (
    SELECT
        id,
        (changepoints).changepoint_indices AS indices
    FROM ts_detect_changepoints_by('multi_series', series_id, ds, value, MAP{})
)
SELECT
    id,
    list_count(indices) AS n_changepoints,
    indices AS changepoint_positions,
    CASE
        WHEN list_count(indices) = 0 THEN 'Stable'
        WHEN list_count(indices) = 1 THEN 'Single regime change'
        WHEN list_count(indices) = 2 THEN 'Three regimes'
        ELSE 'Complex pattern'
    END AS regime_description
FROM detection_results
ORDER BY n_changepoints DESC;

-- ============================================================================
-- CLEANUP
-- ============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS multi_series;
DROP TABLE IF EXISTS retail_demand;
DROP TABLE IF EXISTS sensor_data;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'CHANGEPOINT DETECTION EXAMPLES COMPLETE'
.print '============================================================================='
