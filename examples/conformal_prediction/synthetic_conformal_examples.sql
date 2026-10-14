-- ============================================================================
-- Conformal Prediction Examples - Synthetic Data
-- ============================================================================
-- This script demonstrates conformal prediction with the anofox-forecast
-- extension using *_by table macros.
--
-- Run: ./build/release/duckdb < examples/conformal_prediction/synthetic_conformal_examples.sql
-- ============================================================================

-- Load extension
LOAD anofox_forecast;
-- Phase 16: the conformal macros below read params json-free via
-- params::MAP(VARCHAR, VARCHAR); the json extension is no longer required.

.print '============================================================================='
.print 'CONFORMAL PREDICTION EXAMPLES - Using *_by Table Macros'
.print '============================================================================='

-- ============================================================================
-- SECTION 1: Basic Conformal Prediction for Multiple Series
-- ============================================================================
-- Use ts_conformal_by to compute prediction intervals from backtest data.

.print ''
.print '>>> SECTION 1: Basic Conformal Prediction'
.print '-----------------------------------------------------------------------------'

-- Create multi-series backtest data with actuals, historical forecasts, and new forecasts
CREATE OR REPLACE TABLE backtest_with_forecasts AS
SELECT * FROM (
    -- Product A: Good forecast quality
    SELECT
        'Product_A' AS product_id,
        i AS period,
        100.0 + i * 2.0 + (RANDOM() - 0.5) * 10 AS actual,
        100.0 + i * 2.0 + (RANDOM() - 0.5) * 5 AS forecast,
        NULL::DOUBLE AS point_forecast
    FROM generate_series(1, 40) AS t(i)
    UNION ALL
    -- Product A: New point forecasts to add intervals to
    SELECT
        'Product_A' AS product_id,
        i AS period,
        NULL AS actual,
        NULL AS forecast,
        180.0 + (i - 40) * 2.0 AS point_forecast
    FROM generate_series(41, 45) AS t(i)
    UNION ALL
    -- Product B: More volatile
    SELECT
        'Product_B' AS product_id,
        i AS period,
        200.0 + i * 3.0 + (RANDOM() - 0.5) * 20 AS actual,
        200.0 + i * 3.0 + (RANDOM() - 0.5) * 15 AS forecast,
        NULL::DOUBLE AS point_forecast
    FROM generate_series(1, 40) AS t(i)
    UNION ALL
    -- Product B: New point forecasts
    SELECT
        'Product_B' AS product_id,
        i AS period,
        NULL AS actual,
        NULL AS forecast,
        320.0 + (i - 40) * 3.0 AS point_forecast
    FROM generate_series(41, 45) AS t(i)
    UNION ALL
    -- Product C: Biased forecast
    SELECT
        'Product_C' AS product_id,
        i AS period,
        150.0 + i * 1.5 + (RANDOM() - 0.5) * 8 AS actual,
        140.0 + i * 1.5 + (RANDOM() - 0.5) * 5 AS forecast,
        NULL::DOUBLE AS point_forecast
    FROM generate_series(1, 40) AS t(i)
    UNION ALL
    -- Product C: New point forecasts
    SELECT
        'Product_C' AS product_id,
        i AS period,
        NULL AS actual,
        NULL AS forecast,
        200.0 + (i - 40) * 1.5 AS point_forecast
    FROM generate_series(41, 45) AS t(i)
);

.print 'Backtest data summary:'
SELECT
    product_id,
    COUNT(*) FILTER (WHERE actual IS NOT NULL) AS n_backtest,
    COUNT(*) FILTER (WHERE point_forecast IS NOT NULL) AS n_forecasts,
    ROUND(AVG(actual) FILTER (WHERE actual IS NOT NULL), 2) AS avg_actual
FROM backtest_with_forecasts
GROUP BY product_id
ORDER BY product_id;

-- 1.1: Basic conformal prediction (90% coverage)
.print ''
.print 'Section 1.1: Conformal Prediction Intervals (90% coverage)'

SELECT * FROM ts_conformal_by('backtest_with_forecasts', product_id, period, actual, forecast, point_forecast, MAP{});

-- ============================================================================
-- SECTION 2: Conformal with Different Alpha Levels
-- ============================================================================

.print ''
.print '>>> SECTION 2: Different Coverage Levels'
.print '-----------------------------------------------------------------------------'

-- 2.1: 95% coverage (alpha=0.05)
.print 'Section 2.1: 95% Coverage Intervals'

SELECT * FROM ts_conformal_by('backtest_with_forecasts', product_id, period, actual, forecast, point_forecast,
    MAP{'alpha': '0.05'});

-- 2.2: 80% coverage (alpha=0.20)
.print ''
.print 'Section 2.2: 80% Coverage Intervals'

SELECT * FROM ts_conformal_by('backtest_with_forecasts', product_id, period, actual, forecast, point_forecast,
    MAP{'alpha': '0.20'});

-- ============================================================================
-- SECTION 3: Asymmetric Intervals
-- ============================================================================

.print ''
.print '>>> SECTION 3: Asymmetric Intervals'
.print '-----------------------------------------------------------------------------'

-- Create data with skewed residuals
CREATE OR REPLACE TABLE skewed_backtest AS
SELECT * FROM (
    -- Series with positive skew (over-forecasting more than under)
    SELECT
        'Skewed_Series' AS series_id,
        i AS period,
        100.0 + i + CASE WHEN RANDOM() > 0.3 THEN (RANDOM() * 20) ELSE -(RANDOM() * 5) END AS actual,
        100.0 + i AS forecast,
        NULL::DOUBLE AS point_forecast
    FROM generate_series(1, 50) AS t(i)
    UNION ALL
    SELECT
        'Skewed_Series' AS series_id,
        i AS period,
        NULL AS actual,
        NULL AS forecast,
        150.0 + (i - 50) AS point_forecast
    FROM generate_series(51, 55) AS t(i)
);

-- 3.1: Asymmetric conformal prediction (Phase 16: the old 'asymmetric' key was
-- silently ignored by the pre-16-01 macro -- 'method': 'asymmetric' is the
-- real key that selects the asymmetric branch)
.print 'Section 3.1: Asymmetric Intervals for Skewed Residuals'

SELECT * FROM ts_conformal_by('skewed_backtest', series_id, period, actual, forecast, point_forecast,
    MAP{'method': 'asymmetric'});

-- ============================================================================
-- SECTION 4: Real-World Scenarios
-- ============================================================================

.print ''
.print '>>> SECTION 4: Real-World Scenarios'
.print '-----------------------------------------------------------------------------'

-- 4.1: Retail demand forecasting
.print 'Section 4.1: Retail Demand Forecasting'

CREATE OR REPLACE TABLE retail_backtest AS
SELECT * FROM (
    -- Store A: Consistent demand
    SELECT
        'Store_A' AS store_id,
        DATE '2024-01-01' + INTERVAL (i - 1) DAY AS date,
        i AS period,
        ROUND(500.0 + 10 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 50, 0)::DOUBLE AS actual_demand,
        ROUND(500.0 + 10 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 20, 0)::DOUBLE AS forecast_demand,
        NULL::DOUBLE AS new_forecast
    FROM generate_series(1, 60) AS t(i)
    UNION ALL
    SELECT
        'Store_A' AS store_id,
        DATE '2024-03-01' + INTERVAL (i - 60 - 1) DAY AS date,
        i AS period,
        NULL AS actual_demand,
        NULL AS forecast_demand,
        ROUND(520.0 + 10 * SIN(2 * PI() * i / 7), 0)::DOUBLE AS new_forecast
    FROM generate_series(61, 67) AS t(i)
    UNION ALL
    -- Store B: Higher volume, more variance
    SELECT
        'Store_B' AS store_id,
        DATE '2024-01-01' + INTERVAL (i - 1) DAY AS date,
        i AS period,
        ROUND(2000.0 + 100 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 200, 0)::DOUBLE AS actual_demand,
        ROUND(2000.0 + 100 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 100, 0)::DOUBLE AS forecast_demand,
        NULL::DOUBLE AS new_forecast
    FROM generate_series(1, 60) AS t(i)
    UNION ALL
    SELECT
        'Store_B' AS store_id,
        DATE '2024-03-01' + INTERVAL (i - 60 - 1) DAY AS date,
        i AS period,
        NULL AS actual_demand,
        NULL AS forecast_demand,
        ROUND(2100.0 + 100 * SIN(2 * PI() * i / 7), 0)::DOUBLE AS new_forecast
    FROM generate_series(61, 67) AS t(i)
);

.print 'Store-specific prediction intervals:'
SELECT * FROM ts_conformal_by('retail_backtest', store_id, date, actual_demand, forecast_demand, new_forecast, MAP{});

-- ============================================================================
-- SECTION 5: Using ts_conformal_apply_by with a Calibration Table
-- ============================================================================
-- Phase 16 (D-14): ts_conformal_apply_by now takes a calibration TABLE (the
-- shape ts_conformal_calibrate returns: group_col, method, conformity_score)
-- instead of a bare DOUBLE score. A literal score is applied through a
-- one-row table with group_col = NULL (the "pooled" convention); per-group
-- calibration flows straight from ts_conformal_calibrate.

.print ''
.print '>>> SECTION 5: Apply a Calibration Table'
.print '-----------------------------------------------------------------------------'

-- Create forecast table
CREATE OR REPLACE TABLE future_forecasts AS
SELECT
    product_id,
    period,
    point_forecast
FROM (
    VALUES
        ('Product_A', 46, 190.0), ('Product_A', 47, 192.0), ('Product_A', 48, 194.0),
        ('Product_B', 46, 330.0), ('Product_B', 47, 333.0), ('Product_B', 48, 336.0),
        ('Product_C', 46, 207.5), ('Product_C', 47, 209.0), ('Product_C', 48, 210.5)
) AS t(product_id, period, point_forecast);

-- 5.1: Apply a known literal conformity score (e.g. from an external
-- calibration) to every series, via a one-row group_col = NULL table
.print 'Section 5.1: Applying a literal conformity score of 15.0 to all series'

CREATE OR REPLACE TABLE literal_calibration AS
SELECT NULL::VARCHAR AS group_col, 'symmetric' AS method, 15.0 AS conformity_score;

SELECT * FROM ts_conformal_apply_by('future_forecasts', product_id, period, point_forecast, 'literal_calibration');

-- 5.2: Calibrate per series from the backtest data, then apply each series'
-- own conformity score (the default, non-pooled calibration shape)
.print ''
.print 'Section 5.2: Per-series calibration -> per-series application'

CREATE OR REPLACE TABLE per_series_calibration AS
SELECT * FROM ts_conformal_calibrate('backtest_with_forecasts', product_id, actual, forecast);

.print 'Calibration profile per series:'
SELECT * FROM per_series_calibration ORDER BY group_col;

SELECT * FROM ts_conformal_apply_by('future_forecasts', product_id, period, point_forecast, 'per_series_calibration');

-- ============================================================================
-- SECTION 6: Comparing Interval Widths Across Series
-- ============================================================================

.print ''
.print '>>> SECTION 6: Comparing Interval Widths'
.print '-----------------------------------------------------------------------------'

.print 'Interval width reflects forecast uncertainty per series:'

-- Phase 16: ts_conformal_by now returns one row per (group, date) rather than
-- one row per group with list columns, so interval width is a per-group
-- AGGREGATION of the long-format rows (conformity_score is constant within a
-- group, so MAX/AVG/ANY_VALUE all agree) -- no list_avg() arithmetic.
WITH conformal_results AS (
    SELECT * FROM ts_conformal_by('backtest_with_forecasts', product_id, period, actual, forecast, point_forecast, MAP{})
)
SELECT
    group_col AS product,
    ROUND(MAX(conformity_score), 2) AS score,
    ROUND(AVG(upper) - AVG(lower), 2) AS avg_interval_width,
    CASE
        WHEN MAX(conformity_score) < 10 THEN 'Narrow (good accuracy)'
        WHEN MAX(conformity_score) < 20 THEN 'Medium'
        ELSE 'Wide (high uncertainty)'
    END AS uncertainty_level
FROM conformal_results
GROUP BY group_col
ORDER BY score;

-- ============================================================================
-- Note: ts_conformal_by returns group_col, not id
-- ============================================================================

-- ============================================================================
-- SECTION 7: Adaptive Conformal Prediction (Phase 16 D-08)
-- ============================================================================
-- Adaptive conformal scales the interval by an explicit per-row DIFFICULTY
-- column (e.g. a volatility proxy) at BOTH calibration and prediction time:
-- scores are |residual| / difficulty, and bounds are point +/- q * difficulty
-- USING THE DIFFICULTY AS GIVEN -- no batch-mean normalisation, so a row's
-- interval never depends on which other forecasts happen to be in the same
-- batch.

.print ''
.print '>>> SECTION 7: Adaptive Conformal Prediction'
.print '-----------------------------------------------------------------------------'

-- A per-period rolling volatility proxy stands in for a real difficulty
-- estimate: periods with larger recent swings get a larger difficulty value,
-- so their prediction interval widens accordingly.
CREATE OR REPLACE TABLE adaptive_backtest AS
SELECT
    product_id,
    period,
    actual,
    forecast,
    point_forecast,
    -- difficulty: 1.0 in the calm first half, 3.0 in the volatile second half
    CASE WHEN period <= 20 THEN 1.0 ELSE 3.0 END AS difficulty
FROM backtest_with_forecasts
WHERE product_id = 'Product_A';

.print 'Section 7.1: One-step adaptive conformal via ts_conformal_by'
SELECT * FROM ts_conformal_by(
    'adaptive_backtest', product_id, period, actual, forecast, point_forecast,
    params := {'method': 'adaptive'}, difficulty_col := difficulty
);

.print ''
.print 'Section 7.2: Two-step adaptive conformal (calibrate -> apply)'
CREATE OR REPLACE TABLE adaptive_calibration AS
SELECT * FROM ts_conformal_calibrate(
    'adaptive_backtest', product_id, actual, forecast,
    params := {'method': 'adaptive'}, difficulty_col := difficulty
);

.print 'Adaptive calibration profile:'
SELECT * FROM adaptive_calibration;

CREATE OR REPLACE TABLE adaptive_future AS
SELECT product_id, period, point_forecast,
    CASE WHEN period <= 42 THEN 1.0 ELSE 3.0 END AS difficulty
FROM backtest_with_forecasts
WHERE product_id = 'Product_A' AND point_forecast IS NOT NULL;

SELECT * FROM ts_conformal_apply_by(
    'adaptive_future', product_id, period, point_forecast, 'adaptive_calibration',
    difficulty_col := difficulty
);

-- ============================================================================
-- SECTION 8: Insufficient Calibration (Phase 16 D-05)
-- ============================================================================
-- When ceil((n+1)(1-alpha)) > n, no finite-sample interval exists at this
-- (n, alpha) -- the macros report NULL bounds/score/coverage plus an
-- explicit calibration_insufficient = true flag, instead of silently
-- clamping to the maximum observed residual.

.print ''
.print '>>> SECTION 8: Insufficient Calibration'
.print '-----------------------------------------------------------------------------'

CREATE OR REPLACE TABLE tiny_backtest AS
SELECT * FROM (
    VALUES
        ('Tiny_Series', 1, 101.0, 100.0, NULL::DOUBLE),
        ('Tiny_Series', 2, 102.0, 100.0, NULL::DOUBLE),
        ('Tiny_Series', 3, 103.0, 100.0, NULL::DOUBLE),
        ('Tiny_Series', 4, 104.0, 100.0, NULL::DOUBLE),
        ('Tiny_Series', 5, 105.0, 100.0, NULL::DOUBLE),
        ('Tiny_Series', 6, NULL, NULL, 10.0)
) AS t(series_id, period, actual, forecast, point_forecast);

.print 'With only 5 calibration residuals, alpha 0.1 is INSUFFICIENT (ceil(6*0.9)=6 > 5):'
SELECT * FROM ts_conformal_by('tiny_backtest', series_id, period, actual, forecast, point_forecast,
    MAP{'alpha': '0.1'});

.print ''
.print 'The same series at alpha 0.2 is sufficient (ceil(6*0.8)=5 <= 5):'
SELECT * FROM ts_conformal_by('tiny_backtest', series_id, period, actual, forecast, point_forecast,
    MAP{'alpha': '0.2'});

-- ============================================================================
-- CLEANUP
-- ============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS backtest_with_forecasts;
DROP TABLE IF EXISTS skewed_backtest;
DROP TABLE IF EXISTS retail_backtest;
DROP TABLE IF EXISTS future_forecasts;
DROP TABLE IF EXISTS literal_calibration;
DROP TABLE IF EXISTS per_series_calibration;
DROP TABLE IF EXISTS adaptive_backtest;
DROP TABLE IF EXISTS adaptive_calibration;
DROP TABLE IF EXISTS adaptive_future;
DROP TABLE IF EXISTS tiny_backtest;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'CONFORMAL PREDICTION EXAMPLES COMPLETE'
.print '============================================================================='
