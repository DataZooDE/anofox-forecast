-- =============================================================================
-- Conformal Prediction Examples - Synthetic Data
-- =============================================================================
-- This script demonstrates conformal prediction with the anofox-forecast
-- extension using 5 patterns from basic to advanced.
--
-- Run: ./build/release/duckdb < examples/conformal_prediction/synthetic_conformal_examples.sql
-- =============================================================================

-- Load extension
LOAD anofox_forecast;

.print '============================================================================='
.print 'CONFORMAL PREDICTION EXAMPLES - Synthetic Data'
.print '============================================================================='

-- =============================================================================
-- SECTION 1: Quick Start (Scalar Functions)
-- =============================================================================
-- Use case: Create prediction intervals from residuals.

.print ''
.print '>>> SECTION 1: Quick Start (Scalar Functions)'
.print '-----------------------------------------------------------------------------'

-- Compute conformal intervals from calibration residuals
.print 'Conformal prediction from residuals:'
SELECT
    ts_conformal_predict(
        [0.5, -0.3, 0.8, -0.6, 0.4, -0.2, 0.7, -0.5]::DOUBLE[],  -- calibration residuals
        [100.0, 105.0, 110.0]::DOUBLE[],                          -- point forecasts
        0.1::DOUBLE                                               -- alpha (90% coverage)
    ) AS result;

-- Extract components
.print ''
.print 'Extract interval bounds:'
WITH conf AS (
    SELECT ts_conformal_predict(
        [0.5, -0.3, 0.8, -0.6, 0.4, -0.2, 0.7, -0.5]::DOUBLE[],
        [100.0, 105.0, 110.0]::DOUBLE[],
        0.1::DOUBLE
    ) AS result
)
SELECT
    (result).point AS point_forecasts,
    (result).lower AS lower_bounds,
    (result).upper AS upper_bounds,
    (result).coverage AS target_coverage,
    ROUND((result).conformity_score, 4) AS conformity_score
FROM conf;

-- =============================================================================
-- SECTION 2: Compute Conformity Score (ts_conformal_quantile)
-- =============================================================================
-- Use case: Calibrate conformity score from historical residuals.

.print ''
.print '>>> SECTION 2: Compute Conformity Score (ts_conformal_quantile)'
.print '-----------------------------------------------------------------------------'

-- Create residuals from backtest
CREATE OR REPLACE TABLE backtest_residuals AS
SELECT
    i,
    (RANDOM() - 0.5) * 2.0 AS residual  -- Random residuals between -1 and 1
FROM generate_series(1, 50) AS t(i);

.print 'Compute conformity score for different coverage levels:'
SELECT
    ts_conformal_quantile(LIST(residual), 0.10) AS q_90_coverage,
    ts_conformal_quantile(LIST(residual), 0.05) AS q_95_coverage,
    ts_conformal_quantile(LIST(residual), 0.01) AS q_99_coverage
FROM backtest_residuals;

-- =============================================================================
-- SECTION 3: Full Calibration Workflow (ts_conformal_calibrate)
-- =============================================================================
-- Use case: Calibrate from backtest results table.

.print ''
.print '>>> SECTION 3: Full Calibration Workflow (ts_conformal_calibrate)'
.print '-----------------------------------------------------------------------------'

-- Create backtest results with actual vs forecast
CREATE OR REPLACE TABLE backtest_data AS
SELECT
    i,
    100.0 + i * 2.0 + (RANDOM() - 0.5) * 5 AS actual,
    100.0 + i * 2.0 AS forecast  -- Perfect trend, no noise
FROM generate_series(1, 30) AS t(i);

.print 'Backtest data summary:'
SELECT
    COUNT(*) AS n_points,
    ROUND(AVG(actual - forecast), 4) AS mean_residual,
    ROUND(STDDEV(actual - forecast), 4) AS std_residual
FROM backtest_data;

-- Calibrate conformity score
.print ''
.print 'Calibrate conformity score (90% coverage):'
SELECT * FROM ts_conformal_calibrate('backtest_data', actual, forecast, MAP{'alpha': '0.1'});

-- =============================================================================
-- SECTION 4: Apply to New Forecasts (ts_conformal_apply)
-- =============================================================================
-- Use case: Apply calibrated intervals to new point forecasts.

.print ''
.print '>>> SECTION 4: Apply to New Forecasts (ts_conformal_apply)'
.print '-----------------------------------------------------------------------------'

-- Create future point forecasts
CREATE OR REPLACE TABLE future_forecasts AS
SELECT
    'A' AS series_id,
    31 + i AS step,
    100.0 + (31 + i) * 2.0 AS forecast
FROM generate_series(0, 4) AS t(i);

.print 'Future point forecasts:'
SELECT * FROM future_forecasts ORDER BY step;

-- Get conformity score from calibration
.print ''
.print 'Apply conformity score to forecasts:'
WITH calibration AS (
    SELECT conformity_score
    FROM ts_conformal_calibrate('backtest_data', actual, forecast, MAP{'alpha': '0.1'})
)
SELECT
    group_col AS series_id,
    lower,
    upper
FROM ts_conformal_apply(
    'future_forecasts',
    series_id,
    forecast,
    (SELECT conformity_score FROM calibration)
);

-- =============================================================================
-- SECTION 5: Asymmetric Intervals
-- =============================================================================
-- Use case: Handle skewed residual distributions.

.print ''
.print '>>> SECTION 5: Asymmetric Intervals'
.print '-----------------------------------------------------------------------------'

-- Create skewed residuals (more positive than negative)
.print 'Symmetric vs asymmetric conformal intervals:'
.print ''
.print 'Symmetric method (equal-tailed):'
SELECT
    (ts_conformal_predict(
        [0.1, 0.2, 0.5, 0.8, 1.0, -0.1, -0.05, 0.3]::DOUBLE[],  -- positively skewed
        [50.0]::DOUBLE[],
        0.1::DOUBLE
    )).lower[1] AS lower,
    (ts_conformal_predict(
        [0.1, 0.2, 0.5, 0.8, 1.0, -0.1, -0.05, 0.3]::DOUBLE[],
        [50.0]::DOUBLE[],
        0.1::DOUBLE
    )).upper[1] AS upper;

.print ''
.print 'Asymmetric method (separate quantiles):'
SELECT
    (ts_conformal_predict_asymmetric(
        [0.1, 0.2, 0.5, 0.8, 1.0, -0.1, -0.05, 0.3]::DOUBLE[],  -- positively skewed
        [50.0]::DOUBLE[],
        0.1::DOUBLE
    )).lower[1] AS lower,
    (ts_conformal_predict_asymmetric(
        [0.1, 0.2, 0.5, 0.8, 1.0, -0.1, -0.05, 0.3]::DOUBLE[],
        [50.0]::DOUBLE[],
        0.1::DOUBLE
    )).upper[1] AS upper;

-- =============================================================================
-- CLEANUP
-- =============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS backtest_residuals;
DROP TABLE IF EXISTS backtest_data;
DROP TABLE IF EXISTS future_forecasts;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'CONFORMAL PREDICTION EXAMPLES COMPLETE'
.print '============================================================================='
