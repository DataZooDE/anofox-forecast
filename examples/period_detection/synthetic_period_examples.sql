-- ============================================================================
-- Period Detection Examples - Synthetic Data
-- ============================================================================
-- This script demonstrates period detection with the anofox-forecast
-- extension using the ts_detect_periods_by table macro.
--
-- Run: ./build/release/duckdb < examples/period_detection/synthetic_period_examples.sql
-- ============================================================================

-- Load extension
LOAD anofox_forecast;
INSTALL json;
LOAD json;

-- Enable progress bar for long operations
SET enable_progress_bar = true;

.print '============================================================================='
.print 'PERIOD DETECTION EXAMPLES - Using ts_detect_periods_by'
.print '============================================================================='

-- ============================================================================
-- SECTION 1: Basic Period Detection for Multiple Series
-- ============================================================================
-- Use ts_detect_periods_by to detect seasonal periods across grouped series.

.print ''
.print '>>> SECTION 1: Basic Period Detection'
.print '-----------------------------------------------------------------------------'

-- Create multi-series data with different periods
CREATE OR REPLACE TABLE weekly_data AS
SELECT * FROM (
    -- Series A: Weekly pattern (period=7)
    SELECT
        'series_A' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ds,
        100.0 + 30.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 5 AS value
    FROM generate_series(0, 55) AS t(i)
    UNION ALL
    -- Series B: Bi-weekly pattern (period=14)
    SELECT
        'series_B' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ds,
        100.0 + 30.0 * SIN(2 * PI() * i / 14.0) + (RANDOM() - 0.5) * 5 AS value
    FROM generate_series(0, 55) AS t(i)
    UNION ALL
    -- Series C: Monthly pattern (period=30)
    SELECT
        'series_C' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ds,
        100.0 + 30.0 * SIN(2 * PI() * i / 30.0) + (RANDOM() - 0.5) * 5 AS value
    FROM generate_series(0, 89) AS t(i)
);

.print 'Input data summary:'
SELECT series_id, COUNT(*) AS n_rows FROM weekly_data GROUP BY series_id ORDER BY series_id;

.print ''
.print 'Section 1.1: Default period detection (ensemble method):'
SELECT
    id,
    primary_period AS detected_period,
    method AS method,
    n_periods AS n_periods
FROM ts_detect_periods_by('weekly_data', series_id, ds, value, MAP{});

-- ============================================================================
-- SECTION 2: FFT-Based Detection
-- ============================================================================

.print ''
.print '>>> SECTION 2: FFT-Based Detection'
.print '-----------------------------------------------------------------------------'

-- Create clean sinusoidal signals with known periods
CREATE OR REPLACE TABLE clean_signals AS
SELECT * FROM (
    -- Signal 1: Period 6
    SELECT
        'signal_6' AS signal_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ds,
        100.0 + 50.0 * SIN(2 * PI() * i / 6.0) AS value
    FROM generate_series(0, 71) AS t(i)
    UNION ALL
    -- Signal 2: Period 9
    SELECT
        'signal_9' AS signal_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ds,
        100.0 + 50.0 * SIN(2 * PI() * i / 9.0) AS value
    FROM generate_series(0, 71) AS t(i)
    UNION ALL
    -- Signal 3: Period 12
    SELECT
        'signal_12' AS signal_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ds,
        100.0 + 50.0 * SIN(2 * PI() * i / 12.0) AS value
    FROM generate_series(0, 71) AS t(i)
);

.print 'Signal summary:'
SELECT signal_id, COUNT(*) AS n_rows FROM clean_signals GROUP BY signal_id ORDER BY signal_id;

.print ''
.print 'Section 2.1: FFT period detection on clean sine waves:'
SELECT
    id,
    primary_period AS fft_period,
    method AS method
FROM ts_detect_periods_by('clean_signals', signal_id, ds, value, MAP{'method': 'fft'});

-- ============================================================================
-- SECTION 3: ACF-Based Detection (Robust to Noise)
-- ============================================================================

.print ''
.print '>>> SECTION 3: ACF-Based Detection (Robust to Noise)'
.print '-----------------------------------------------------------------------------'

-- Create noisy data with known periods
CREATE OR REPLACE TABLE noisy_data AS
SELECT * FROM (
    -- Noisy series A: Period 10 with high noise
    SELECT
        'noisy_A' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ds,
        100.0 + 20.0 * SIN(2 * PI() * i / 10.0) + (RANDOM() - 0.5) * 30 AS value
    FROM generate_series(0, 99) AS t(i)
    UNION ALL
    -- Noisy series B: Period 14 with high noise
    SELECT
        'noisy_B' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ds,
        100.0 + 20.0 * SIN(2 * PI() * i / 14.0) + (RANDOM() - 0.5) * 30 AS value
    FROM generate_series(0, 99) AS t(i)
);

.print 'Noisy data summary:'
SELECT series_id, COUNT(*) AS n_rows FROM noisy_data GROUP BY series_id ORDER BY series_id;

.print ''
.print 'Section 3.1: ACF period detection on noisy data:'
SELECT
    id,
    primary_period AS acf_period,
    method AS method
FROM ts_detect_periods_by('noisy_data', series_id, ds, value, MAP{'method': 'acf'});

-- ============================================================================
-- SECTION 4: Multiple Periods Detection
-- ============================================================================

.print ''
.print '>>> SECTION 4: Multiple Periods Detection'
.print '-----------------------------------------------------------------------------'

-- Create data with two seasonal patterns (weekly + monthly)
CREATE OR REPLACE TABLE dual_seasonal AS
SELECT * FROM (
    -- Series with weekly + monthly patterns
    SELECT
        'dual_pattern' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ds,
        100.0
        + 30.0 * SIN(2 * PI() * i / 7.0)   -- weekly
        + 15.0 * SIN(2 * PI() * i / 30.0)  -- monthly
        + (RANDOM() - 0.5) * 5 AS value
    FROM generate_series(0, 119) AS t(i)
);

.print 'Dual seasonal data: 120 days with weekly + monthly patterns'

.print ''
.print 'Section 4.1: Detecting multiple periods:'
SELECT
    id,
    primary_period AS primary_period,
    n_periods AS n_periods_found
FROM ts_detect_periods_by('dual_seasonal', series_id, ds, value, MAP{'method': 'multi'});

-- ============================================================================
-- SECTION 5: Comparing Detection Methods
-- ============================================================================

.print ''
.print '>>> SECTION 5: Comparing Detection Methods'
.print '-----------------------------------------------------------------------------'

-- Create standard test data with known period=7
CREATE OR REPLACE TABLE compare_data AS
SELECT
    'test_series' AS series_id,
    '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ds,
    100.0 + 25.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 10 AS value
FROM generate_series(0, 69) AS t(i);

.print 'Comparing different detection methods (expected period=7):'

-- FFT method
.print ''
.print 'Section 5.1: FFT method:'
SELECT
    id,
    primary_period AS detected_period,
    method AS method
FROM ts_detect_periods_by('compare_data', series_id, ds, value, MAP{'method': 'fft'});

-- ACF method
.print ''
.print 'Section 5.2: ACF method:'
SELECT
    id,
    primary_period AS detected_period,
    method AS method
FROM ts_detect_periods_by('compare_data', series_id, ds, value, MAP{'method': 'acf'});

-- Autoperiod method
.print ''
.print 'Section 5.3: Autoperiod method:'
SELECT
    id,
    primary_period AS detected_period,
    method AS method
FROM ts_detect_periods_by('compare_data', series_id, ds, value, MAP{'method': 'autoperiod'});

-- Ensemble (default) method
.print ''
.print 'Section 5.4: Ensemble method (default):'
SELECT
    id,
    primary_period AS detected_period,
    method AS method
FROM ts_detect_periods_by('compare_data', series_id, ds, value, MAP{});

-- ============================================================================
-- SECTION 6: Real-World Scenarios
-- ============================================================================

.print ''
.print '>>> SECTION 6: Real-World Scenarios'
.print '-----------------------------------------------------------------------------'

-- Create retail sales data with weekly patterns
CREATE OR REPLACE TABLE retail_sales AS
SELECT * FROM (
    -- Store A: Strong weekly pattern
    SELECT
        'Store_A' AS store_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        ROUND(
            5000.0
            + i * 5  -- growth trend
            + 1500.0 * SIN(2 * PI() * i / 7.0)  -- weekly pattern
            + (RANDOM() - 0.5) * 300
        , 0)::DOUBLE AS sales
    FROM generate_series(0, 89) AS t(i)
    UNION ALL
    -- Store B: Bi-weekly pattern
    SELECT
        'Store_B' AS store_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        ROUND(
            3000.0
            + i * 3
            + 800.0 * SIN(2 * PI() * i / 14.0)  -- bi-weekly pattern
            + (RANDOM() - 0.5) * 200
        , 0)::DOUBLE AS sales
    FROM generate_series(0, 89) AS t(i)
    UNION ALL
    -- Store C: Random (no clear period)
    SELECT
        'Store_C' AS store_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        ROUND(
            4000.0
            + i * 2
            + (RANDOM() - 0.5) * 500
        , 0)::DOUBLE AS sales
    FROM generate_series(0, 89) AS t(i)
);

.print 'Section 6.1: Retail Sales Period Detection'

SELECT
    id AS store,
    primary_period AS detected_period,
    method AS method,
    CASE
        WHEN primary_period BETWEEN 6 AND 8 THEN 'Weekly'
        WHEN primary_period BETWEEN 13 AND 15 THEN 'Bi-weekly'
        WHEN primary_period BETWEEN 28 AND 32 THEN 'Monthly'
        ELSE 'Other/None'
    END AS pattern_type
FROM ts_detect_periods_by('retail_sales', store_id, date, sales, MAP{});

-- ============================================================================
-- CLEANUP
-- ============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS weekly_data;
DROP TABLE IF EXISTS clean_signals;
DROP TABLE IF EXISTS noisy_data;
DROP TABLE IF EXISTS dual_seasonal;
DROP TABLE IF EXISTS compare_data;
DROP TABLE IF EXISTS retail_sales;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'PERIOD DETECTION EXAMPLES COMPLETE'
.print '============================================================================='
