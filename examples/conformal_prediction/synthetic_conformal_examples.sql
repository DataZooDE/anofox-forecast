-- =============================================================================
-- Conformal Prediction Examples - Synthetic Data
-- =============================================================================
-- This script demonstrates conformal prediction with the anofox-forecast
-- extension. Organized into tracks based on your needs:
--
-- TRACK 1: TL;DR (30 seconds)     - Copy-paste for immediate results
-- TRACK 2: Learn/Apply Pattern   - Production-ready two-step workflow
-- TRACK 3: Evaluation & Metrics  - Measure interval quality
-- TRACK 4: Advanced ML (XGBoost) - Integration with external models
-- TRACK 5: Legacy Scalar API     - One-shot convenience functions
--
-- Run: ./build/release/duckdb < examples/conformal_prediction/synthetic_conformal_examples.sql
-- =============================================================================

-- Load extension
LOAD anofox_forecast;

.print '============================================================================='
.print 'CONFORMAL PREDICTION EXAMPLES - Synthetic Data'
.print '============================================================================='

-- =============================================================================
-- TRACK 1: TL;DR (People in a Hurry)
-- =============================================================================
-- Copy this block, replace with your data, done.

.print ''
.print '>>> TRACK 1: TL;DR - Get Prediction Intervals in 30 Seconds'
.print '-----------------------------------------------------------------------------'

-- One-liner: residuals + forecasts -> intervals
.print 'Instant prediction intervals (90% coverage):'
SELECT
    ts_conformal_predict(
        [0.5, -0.3, 0.8, -0.6, 0.4, -0.2, 0.7, -0.5]::DOUBLE[],  -- your backtest residuals
        [100.0, 105.0, 110.0]::DOUBLE[],                          -- your point forecasts
        0.1                                                        -- alpha (0.1 = 90% coverage)
    ) AS result;

-- Extract just the bounds you need
.print ''
.print 'Extract lower/upper bounds:'
WITH intervals AS (
    SELECT ts_conformal_predict(
        [0.5, -0.3, 0.8, -0.6, 0.4, -0.2, 0.7, -0.5]::DOUBLE[],
        [100.0, 105.0, 110.0]::DOUBLE[],
        0.1
    ) AS r
)
SELECT
    (r).lower AS lower_bounds,
    (r).upper AS upper_bounds
FROM intervals;

-- =============================================================================
-- TRACK 2: Learn/Apply Pattern (Production Workflow)
-- =============================================================================
-- Best practice: Learn once, apply many times.
-- Separates calibration from inference for efficiency.

.print ''
.print '>>> TRACK 2: Learn/Apply Pattern (Production Workflow)'
.print '-----------------------------------------------------------------------------'

-- Step 1: LEARN - Compute calibration profile from historical residuals
.print 'Step 1: Learn calibration profile from residuals'
SELECT
    ts_conformal_learn(
        [0.5, -0.3, 0.8, -0.6, 0.4, -0.2, 0.7, -0.5, 0.3, -0.4]::DOUBLE[],  -- residuals
        [0.1, 0.05]::DOUBLE[],   -- alphas: 90% and 95% coverage
        'symmetric',              -- method: symmetric | asymmetric | adaptive
        'split'                   -- strategy: split | crossval | jackknife_plus
    ) AS profile;

-- Step 2: APPLY - Use profile to create intervals for new forecasts
.print ''
.print 'Step 2: Apply profile to new forecasts'
WITH calibration AS (
    SELECT ts_conformal_learn(
        [0.5, -0.3, 0.8, -0.6, 0.4, -0.2, 0.7, -0.5, 0.3, -0.4]::DOUBLE[],
        [0.1, 0.05]::DOUBLE[],
        'symmetric',
        'split'
    ) AS profile
)
SELECT
    ts_conformal_apply(
        [100.0, 105.0, 110.0]::DOUBLE[],  -- new forecasts
        profile
    ) AS intervals
FROM calibration;

-- Jackknife+ strategy: stores full distribution for tighter intervals
.print ''
.print 'Jackknife+ strategy (tighter intervals, stores full distribution):'
SELECT
    ts_conformal_learn(
        [0.5, -0.3, 0.8, -0.6, 0.4, -0.2, 0.7, -0.5, 0.3, -0.4]::DOUBLE[],
        [0.1]::DOUBLE[],
        'symmetric',
        'jackknife_plus'  -- stores sorted residuals for inference-time quantiles
    ) AS profile;

-- =============================================================================
-- TRACK 3: Evaluation & Metrics
-- =============================================================================
-- Measure how good your intervals are.

.print ''
.print '>>> TRACK 3: Evaluation & Metrics'
.print '-----------------------------------------------------------------------------'

-- Create test data with known intervals
CREATE OR REPLACE TEMP TABLE test_intervals AS
SELECT
    [100.0, 102.0, 104.0, 106.0, 108.0]::DOUBLE[] AS actuals,
    [98.0, 100.0, 102.0, 103.0, 105.0]::DOUBLE[] AS lower_bounds,
    [102.0, 104.0, 106.0, 109.0, 111.0]::DOUBLE[] AS upper_bounds;

-- ts_conformal_coverage: What fraction of actuals fall within intervals?
.print 'Empirical coverage (target was 90%):'
SELECT
    ts_conformal_coverage(actuals, lower_bounds, upper_bounds) AS coverage
FROM test_intervals;

-- ts_conformal_evaluate: Comprehensive evaluation metrics
.print ''
.print 'Comprehensive evaluation (coverage, width, Winkler score):'
SELECT
    ts_conformal_evaluate(actuals, lower_bounds, upper_bounds, 0.1) AS evaluation
FROM test_intervals;

-- Unpack the evaluation struct
.print ''
.print 'Detailed evaluation metrics:'
WITH eval AS (
    SELECT ts_conformal_evaluate(actuals, lower_bounds, upper_bounds, 0.1) AS e
    FROM test_intervals
)
SELECT
    ROUND((e).coverage, 3) AS coverage,
    ROUND((e).violation_rate, 3) AS violation_rate,
    ROUND((e).mean_width, 3) AS mean_width,
    ROUND((e).winkler_score, 3) AS winkler_score,
    (e).n_observations AS n_obs
FROM eval;

-- ts_mean_interval_width: Average interval width
.print ''
.print 'Mean interval width:'
SELECT
    ts_mean_interval_width(lower_bounds, upper_bounds) AS mean_width
FROM test_intervals;

DROP TABLE test_intervals;

-- =============================================================================
-- TRACK 4: Advanced ML Integration (XGBoost Example)
-- =============================================================================
-- How to add prediction intervals to any ML model (XGBoost, LightGBM, etc.)
-- Key insight: Conformal prediction only needs residuals from backtest.

.print ''
.print '>>> TRACK 4: Advanced ML Integration (XGBoost Workflow)'
.print '-----------------------------------------------------------------------------'

-- Simulate XGBoost backtest results (in practice, run your model)
CREATE OR REPLACE TEMP TABLE xgboost_backtest AS
SELECT
    i AS period,
    -- Simulated actual values: trend + seasonality + noise
    100.0 + i * 0.5 + 10 * SIN(i * 0.5) + (RANDOM() - 0.5) * 5 AS actual,
    -- Simulated XGBoost predictions (slightly biased, realistic)
    100.0 + i * 0.5 + 10 * SIN(i * 0.5) + (RANDOM() - 0.5) * 2 AS xgb_prediction
FROM generate_series(1, 100) AS t(i);

-- Step A: Compute residuals from XGBoost backtest
.print 'XGBoost backtest residual statistics:'
SELECT
    COUNT(*) AS n_samples,
    ROUND(AVG(actual - xgb_prediction), 4) AS mean_residual,
    ROUND(STDDEV(actual - xgb_prediction), 4) AS std_residual,
    ROUND(MIN(actual - xgb_prediction), 4) AS min_residual,
    ROUND(MAX(actual - xgb_prediction), 4) AS max_residual
FROM xgboost_backtest;

-- Step B: Learn calibration profile from XGBoost residuals
.print ''
.print 'Learn calibration profile from XGBoost residuals:'
CREATE OR REPLACE TEMP TABLE xgb_profile AS
SELECT ts_conformal_learn(
    (SELECT LIST(actual - xgb_prediction) FROM xgboost_backtest),
    [0.1, 0.05, 0.01]::DOUBLE[],  -- 90%, 95%, 99% coverage
    'symmetric',
    'split'
) AS profile;

SELECT profile FROM xgb_profile;

-- Step C: Simulate new XGBoost predictions and add intervals
CREATE OR REPLACE TEMP TABLE xgboost_forecast AS
SELECT
    101 + i AS period,
    -- Simulated future XGBoost predictions
    100.0 + (101 + i) * 0.5 + 10 * SIN((101 + i) * 0.5) AS xgb_prediction
FROM generate_series(0, 6) AS t(i);

.print ''
.print 'XGBoost forecasts with prediction intervals:'
WITH forecast_agg AS (
    SELECT LIST(xgb_prediction ORDER BY period) AS forecasts FROM xgboost_forecast
),
intervals AS (
    SELECT ts_conformal_apply(forecasts, profile) AS iv
    FROM forecast_agg, xgb_profile
)
SELECT
    (iv).point AS point_forecasts,
    (iv).coverage AS coverage_levels,
    (iv).lower[1:3] AS lower_90_first3,
    (iv).upper[1:3] AS upper_90_first3
FROM intervals;

DROP TABLE xgboost_backtest;
DROP TABLE xgb_profile;
DROP TABLE xgboost_forecast;

-- =============================================================================
-- TRACK 5: Legacy Scalar API (One-Shot Functions)
-- =============================================================================
-- Convenience functions for quick exploration.

.print ''
.print '>>> TRACK 5: Legacy Scalar API (One-Shot Functions)'
.print '-----------------------------------------------------------------------------'

-- ts_conformal_quantile: Just get the conformity score
.print 'Conformity scores for different coverage levels:'
SELECT
    ROUND(ts_conformal_quantile([0.5, -0.3, 0.8, -0.6, 0.4, -0.2, 0.7, -0.5]::DOUBLE[], 0.10), 4) AS q90,
    ROUND(ts_conformal_quantile([0.5, -0.3, 0.8, -0.6, 0.4, -0.2, 0.7, -0.5]::DOUBLE[], 0.05), 4) AS q95,
    ROUND(ts_conformal_quantile([0.5, -0.3, 0.8, -0.6, 0.4, -0.2, 0.7, -0.5]::DOUBLE[], 0.01), 4) AS q99;

-- ts_conformal_intervals: Apply a known score to forecasts
.print ''
.print 'Apply conformity score to forecasts:'
SELECT
    ts_conformal_intervals(
        [100.0, 105.0, 110.0]::DOUBLE[],  -- forecasts
        0.8                                 -- conformity score
    ) AS intervals;

-- ts_conformal_predict_asymmetric: Handle skewed distributions
.print ''
.print 'Asymmetric intervals for skewed residuals:'
WITH skewed AS (
    SELECT [0.1, 0.2, 0.5, 0.8, 1.2, 1.5, -0.1, -0.05]::DOUBLE[] AS residuals
)
SELECT
    'Symmetric' AS method,
    (ts_conformal_predict(residuals, [50.0]::DOUBLE[], 0.1)).lower[1] AS lower,
    (ts_conformal_predict(residuals, [50.0]::DOUBLE[], 0.1)).upper[1] AS upper
FROM skewed
UNION ALL
SELECT
    'Asymmetric' AS method,
    (ts_conformal_predict_asymmetric(residuals, [50.0]::DOUBLE[], 0.1)).lower[1] AS lower,
    (ts_conformal_predict_asymmetric(residuals, [50.0]::DOUBLE[], 0.1)).upper[1] AS upper
FROM skewed;

-- =============================================================================
-- TRACK 6: Table Macro Workflow (Grouped Data)
-- =============================================================================
-- Process multiple series efficiently using table macros.

.print ''
.print '>>> TRACK 6: Table Macro Workflow (Multiple Series)'
.print '-----------------------------------------------------------------------------'

-- Create backtest data for multiple product series
CREATE OR REPLACE TEMP TABLE multi_series_backtest AS
SELECT
    CASE WHEN i <= 30 THEN 'Product_A'
         WHEN i <= 60 THEN 'Product_B'
         ELSE 'Product_C' END AS product_id,
    i % 30 + 1 AS period,
    100.0 + (i % 30) * 2.0 + (RANDOM() - 0.5) * 8 AS actual,
    100.0 + (i % 30) * 2.0 AS forecast
FROM generate_series(1, 90) AS t(i);

.print 'Multi-series backtest summary:'
SELECT
    product_id,
    COUNT(*) AS n_points,
    ROUND(AVG(actual - forecast), 3) AS mean_residual,
    ROUND(STDDEV(actual - forecast), 3) AS std_residual
FROM multi_series_backtest
GROUP BY product_id
ORDER BY product_id;

-- Calibrate per-series using table macro
.print ''
.print 'Per-series calibration (90% coverage):'
SELECT * FROM ts_conformal_calibrate(
    'multi_series_backtest',
    actual,
    forecast,
    MAP{'alpha': '0.1', 'group_by': 'product_id'}
);

DROP TABLE multi_series_backtest;

-- =============================================================================
-- QUICK REFERENCE CARD
-- =============================================================================
.print ''
.print '============================================================================='
.print 'QUICK REFERENCE CARD'
.print '============================================================================='
.print ''
.print 'NEW API (Learn/Apply Pattern):'
.print '  ts_conformal_learn(residuals[], alphas[], method, strategy) -> Profile'
.print '  ts_conformal_apply(forecasts[], profile) -> Intervals'
.print '  ts_conformal_coverage(actuals[], lower[], upper[]) -> DOUBLE'
.print '  ts_conformal_evaluate(actuals[], lower[], upper[], alpha) -> Metrics'
.print ''
.print 'Methods: symmetric | asymmetric | adaptive'
.print 'Strategies: split | crossval | jackknife_plus'
.print ''
.print 'LEGACY API (One-Shot):'
.print '  ts_conformal_predict(residuals[], forecasts[], alpha) -> Full result'
.print '  ts_conformal_predict_asymmetric(...) -> Asymmetric intervals'
.print '  ts_conformal_quantile(residuals[], alpha) -> Conformity score'
.print '  ts_conformal_intervals(forecasts[], score) -> Intervals'
.print ''
.print 'TABLE MACROS (Grouped Data):'
.print '  ts_conformal_calibrate(table, actual_col, forecast_col, options)'
.print '  ts_conformal_apply(table, group_col, forecast_col, score)'
.print ''
.print '============================================================================='
.print 'CONFORMAL PREDICTION EXAMPLES COMPLETE'
.print '============================================================================='
