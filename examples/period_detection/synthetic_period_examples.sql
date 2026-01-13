-- =============================================================================
-- Period Detection Examples - Synthetic Data
-- =============================================================================
-- This script demonstrates period detection with the anofox-forecast
-- extension using 5 patterns.
--
-- Run: ./build/release/duckdb < examples/period_detection/synthetic_period_examples.sql
-- =============================================================================

-- Load extension
LOAD anofox_forecast;

.print '============================================================================='
.print 'PERIOD DETECTION EXAMPLES - Synthetic Data'
.print '============================================================================='

-- =============================================================================
-- SECTION 1: Quick Start (ts_detect_periods)
-- =============================================================================

.print ''
.print '>>> SECTION 1: Quick Start (ts_detect_periods)'
.print '-----------------------------------------------------------------------------'

-- Create data with clear period=7 (weekly pattern)
CREATE OR REPLACE TABLE weekly_data AS
SELECT
    i,
    100.0 + 30.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 5 AS value
FROM generate_series(0, 55) AS t(i);

.print 'Detecting period in weekly data:'
SELECT
    (ts_detect_periods(LIST(value ORDER BY i))).primary_period AS detected_period,
    (ts_detect_periods(LIST(value ORDER BY i))).method AS method,
    (ts_detect_periods(LIST(value ORDER BY i))).n_periods AS n_periods
FROM weekly_data;

-- =============================================================================
-- SECTION 2: FFT-Based Detection
-- =============================================================================

.print ''
.print '>>> SECTION 2: FFT-Based Detection'
.print '-----------------------------------------------------------------------------'

-- Create clean sinusoidal data
CREATE OR REPLACE TABLE clean_sine AS
SELECT
    i,
    100.0 + 50.0 * SIN(2 * PI() * i / 12.0) AS value  -- period = 12
FROM generate_series(0, 71) AS t(i);

.print 'FFT period detection on clean sine wave (period=12):'
SELECT
    (ts_estimate_period_fft(LIST(value ORDER BY i))).period AS fft_period,
    (ts_estimate_period_fft(LIST(value ORDER BY i))).frequency AS frequency,
    (ts_estimate_period_fft(LIST(value ORDER BY i))).power AS power
FROM clean_sine;

-- =============================================================================
-- SECTION 3: ACF-Based Detection
-- =============================================================================

.print ''
.print '>>> SECTION 3: ACF-Based Detection'
.print '-----------------------------------------------------------------------------'

-- Create noisy data
CREATE OR REPLACE TABLE noisy_data AS
SELECT
    i,
    100.0 + 20.0 * SIN(2 * PI() * i / 10.0) + (RANDOM() - 0.5) * 30 AS value  -- period = 10
FROM generate_series(0, 99) AS t(i);

.print 'ACF period detection on noisy data (period=10):'
SELECT
    (ts_estimate_period_acf(LIST(value ORDER BY i))).period AS acf_period,
    (ts_estimate_period_acf(LIST(value ORDER BY i))).confidence AS confidence
FROM noisy_data;

-- =============================================================================
-- SECTION 4: Multiple Periods
-- =============================================================================

.print ''
.print '>>> SECTION 4: Multiple Periods'
.print '-----------------------------------------------------------------------------'

-- Create data with two seasonal patterns
CREATE OR REPLACE TABLE dual_seasonal AS
SELECT
    i,
    100.0
    + 30.0 * SIN(2 * PI() * i / 7.0)   -- weekly
    + 15.0 * SIN(2 * PI() * i / 30.0)  -- monthly
    + (RANDOM() - 0.5) * 5 AS value
FROM generate_series(0, 119) AS t(i);

.print 'Detecting multiple periods (weekly + monthly):'
SELECT
    (ts_detect_multiple_periods(LIST(value ORDER BY i))).periods AS detected_periods,
    (ts_detect_multiple_periods(LIST(value ORDER BY i))).n_periods AS n_periods
FROM dual_seasonal;

-- =============================================================================
-- SECTION 5: Compare Methods
-- =============================================================================

.print ''
.print '>>> SECTION 5: Compare Methods'
.print '-----------------------------------------------------------------------------'

-- Create standard test data
CREATE OR REPLACE TABLE compare_data AS
SELECT
    i,
    100.0 + 25.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 10 AS value
FROM generate_series(0, 69) AS t(i);

.print 'Comparing different detection methods (expected period=7):'
SELECT
    ROUND((ts_estimate_period_fft(LIST(value ORDER BY i))).period, 2) AS fft_period,
    ROUND((ts_estimate_period_acf(LIST(value ORDER BY i))).period, 2) AS acf_period,
    ROUND((ts_autoperiod(LIST(value ORDER BY i))).period, 2) AS autoperiod,
    ROUND((ts_detect_periods(LIST(value ORDER BY i))).primary_period, 2) AS ensemble
FROM compare_data;

-- =============================================================================
-- CLEANUP
-- =============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS weekly_data;
DROP TABLE IF EXISTS clean_sine;
DROP TABLE IF EXISTS noisy_data;
DROP TABLE IF EXISTS dual_seasonal;
DROP TABLE IF EXISTS compare_data;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'PERIOD DETECTION EXAMPLES COMPLETE'
.print '============================================================================='
