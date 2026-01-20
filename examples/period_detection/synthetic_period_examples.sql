-- =============================================================================
-- Period Detection Examples - Synthetic Data
-- =============================================================================
-- This script demonstrates period detection with the anofox-forecast
-- extension using 5 patterns with multi-series support.
--
-- Run: ./build/release/duckdb < examples/period_detection/synthetic_period_examples.sql
-- =============================================================================

-- Load extension
LOAD anofox_forecast;

-- Enable progress bar for long operations
SET enable_progress_bar = true;

.print '============================================================================='
.print 'PERIOD DETECTION EXAMPLES - Synthetic Data'
.print '============================================================================='

-- =============================================================================
-- SECTION 1: Quick Start (Multi-Series with ts_detect_periods_by)
-- =============================================================================

.print ''
.print '>>> SECTION 1: Quick Start (Multi-Series with ts_detect_periods_by)'
.print '-----------------------------------------------------------------------------'

-- Create multi-series data with different periods
-- series_A has period=7 (weekly), series_B has period=12
CREATE OR REPLACE TABLE weekly_data AS
SELECT
    CASE WHEN i < 56 THEN 'series_A' ELSE 'series_B' END AS series_id,
    '2024-01-01'::TIMESTAMP + INTERVAL (i % 56) DAY AS ds,
    CASE WHEN i < 56
        THEN 100.0 + 30.0 * SIN(2 * PI() * (i % 56) / 7.0)   -- period=7
        ELSE 100.0 + 30.0 * SIN(2 * PI() * (i % 56) / 12.0)  -- period=12
    END + (RANDOM() - 0.5) * 5 AS value
FROM generate_series(0, 111) AS t(i);

.print 'Input data summary:'
SELECT series_id, COUNT(*) AS n_rows FROM weekly_data GROUP BY series_id ORDER BY series_id;

.print ''
.print 'Detecting periods in multi-series data:'
SELECT
    id,
    (periods).primary_period AS detected_period,
    (periods).method AS method,
    (periods).n_periods AS n_periods
FROM ts_detect_periods_by('weekly_data', series_id, ds, value, MAP{});

-- =============================================================================
-- SECTION 2: FFT-Based Detection (Multi-Series)
-- =============================================================================

.print ''
.print '>>> SECTION 2: FFT-Based Detection (Multi-Series)'
.print '-----------------------------------------------------------------------------'

-- Create 3 clean sinusoidal signals with periods 6, 9, 12
CREATE OR REPLACE TABLE clean_sine AS
SELECT
    'signal_' || ((i / 72) + 1) AS signal_id,
    '2024-01-01'::TIMESTAMP + INTERVAL (i % 72) DAY AS ds,
    100.0 + 50.0 * SIN(2 * PI() * (i % 72) / (6 + (i / 72) * 3)) AS value  -- periods: 6, 9, 12
FROM generate_series(0, 215) AS t(i);

.print 'Signal summary:'
SELECT signal_id, COUNT(*) AS n_rows FROM clean_sine GROUP BY signal_id ORDER BY signal_id;

.print ''
.print 'FFT period detection on clean sine waves:'
SELECT
    id,
    (periods).primary_period AS fft_period,
    (periods).method AS method
FROM ts_detect_periods_by('clean_sine', signal_id, ds, value, {'method': 'fft'});

-- =============================================================================
-- SECTION 3: ACF-Based Detection (Multi-Series)
-- =============================================================================

.print ''
.print '>>> SECTION 3: ACF-Based Detection (Multi-Series)'
.print '-----------------------------------------------------------------------------'

-- Create noisy data with periods 10 and 14
CREATE OR REPLACE TABLE noisy_data AS
SELECT
    CASE WHEN i < 100 THEN 'noisy_A' ELSE 'noisy_B' END AS series_id,
    '2024-01-01'::TIMESTAMP + INTERVAL (i % 100) DAY AS ds,
    CASE WHEN i < 100
        THEN 100.0 + 20.0 * SIN(2 * PI() * (i % 100) / 10.0)  -- period=10
        ELSE 100.0 + 20.0 * SIN(2 * PI() * (i % 100) / 14.0)  -- period=14
    END + (RANDOM() - 0.5) * 30 AS value
FROM generate_series(0, 199) AS t(i);

.print 'Noisy data summary:'
SELECT series_id, COUNT(*) AS n_rows FROM noisy_data GROUP BY series_id ORDER BY series_id;

.print ''
.print 'ACF period detection on noisy data:'
SELECT
    id,
    (periods).primary_period AS acf_period,
    (periods).method AS method
FROM ts_detect_periods_by('noisy_data', series_id, ds, value, {'method': 'acf'});

-- =============================================================================
-- SECTION 4: Multiple Periods (Single Series)
-- =============================================================================

.print ''
.print '>>> SECTION 4: Multiple Periods (Single Series)'
.print '-----------------------------------------------------------------------------'

-- Create data with two seasonal patterns (weekly + monthly)
CREATE OR REPLACE TABLE dual_seasonal AS
SELECT
    '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ds,
    100.0
    + 30.0 * SIN(2 * PI() * i / 7.0)   -- weekly
    + 15.0 * SIN(2 * PI() * i / 30.0)  -- monthly
    + (RANDOM() - 0.5) * 5 AS value
FROM generate_series(0, 119) AS t(i);

.print 'Dual seasonal data: 120 days with weekly + monthly patterns'

.print ''
.print 'Detecting multiple periods:'
SELECT
    (periods).primary_period,
    (periods).n_periods AS n_periods
FROM ts_detect_periods('dual_seasonal', ds, value, {'method': 'multi'});

-- =============================================================================
-- SECTION 5: Compare Methods (Multi-Series)
-- =============================================================================

.print ''
.print '>>> SECTION 5: Compare Methods'
.print '-----------------------------------------------------------------------------'

-- Create standard test data with known period=7
CREATE OR REPLACE TABLE compare_data AS
SELECT
    'test_series' AS series_id,
    '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ds,
    100.0 + 25.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 10 AS value
FROM generate_series(0, 69) AS t(i);

.print 'Comparing different detection methods (expected period=7):'
SELECT
    'fft' AS method,
    (SELECT (periods).primary_period FROM ts_detect_periods_by('compare_data', series_id, ds, value, {'method': 'fft'})) AS detected_period
UNION ALL
SELECT
    'acf' AS method,
    (SELECT (periods).primary_period FROM ts_detect_periods_by('compare_data', series_id, ds, value, {'method': 'acf'})) AS detected_period
UNION ALL
SELECT
    'autoperiod' AS method,
    (SELECT (periods).primary_period FROM ts_detect_periods_by('compare_data', series_id, ds, value, {'method': 'autoperiod'})) AS detected_period
UNION ALL
SELECT
    'ensemble (default)' AS method,
    (SELECT (periods).primary_period FROM ts_detect_periods_by('compare_data', series_id, ds, value, MAP{})) AS detected_period;

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
