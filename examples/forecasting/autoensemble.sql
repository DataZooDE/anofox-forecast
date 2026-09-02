-- ============================================================================
-- AutoEnsemble Forecasting Example — Phase 4 (ENS-01, COMB-01)
-- ============================================================================
-- Demonstrates ts_forecast_by() with method='AutoEnsemble', added in Phase 4.
--
-- AutoEnsemble auto-fits AutoARIMA, AutoETS, and AutoTheta; ranks the three
-- candidates by in-sample MSE (ascending); combines the top-K members using
-- the specified CombinationMethod.
--
-- Parameters (via params MAP or STRUCT):
--   top_k              (int)    Number of top-ranked models to combine. Default: 3.
--   combination_method (string) How to combine member forecasts. Default: 'mean'.
--                               Accepted: 'mean', 'median', 'weighted_mse',
--                               'inverse_aic', 'stacking', 'horizon_adaptive'
--                               (plus common aliases for each).
--   seasonal_period    (int)    Seasonal period shared with other models. 0 = non-seasonal.
--
-- Prediction intervals: yhat_lower and yhat_upper are NULL in Phase 4.
-- Ensemble prediction intervals are deferred to Phase 6 (EPI-01).
--
-- Mean internal-consistency cross-check (DoD, COMB-01):
--   With combination_method='mean' and top_k=3, AutoEnsemble's point forecast
--   equals the arithmetic mean of the three independent member forecasts, PROVIDED
--   all three Auto* models converge. This holds ONLY when:
--     1. top_k >= 3 (all three candidates are selected)
--     2. All three models fit successfully (no convergence failures)
--     3. The same seasonal_period is used in both the ensemble and member calls
--   This example uses a clean 60-observation linear series where all three members
--   reliably converge, satisfying all three conditions.
--
-- Run: ./build/release/duckdb -unsigned < examples/forecasting/autoensemble.sql
-- ============================================================================

LOAD anofox_forecast;

.print '============================================================================='
.print 'AUTOENSEMBLE FORECASTING EXAMPLES — Phase 4 (ENS-01, COMB-01)'
.print '============================================================================='

-- ============================================================================
-- SECTION 1: Mean Combination Cross-Check (DoD, COMB-01)
-- ============================================================================
-- Proves: AutoEnsemble(mean, top_k=3) yhat == arithmetic mean of
-- AutoARIMA + AutoETS + AutoTheta on the same non-seasonal series.
-- ============================================================================

.print ''
.print '>>> SECTION 1: Mean Combination Internal-Consistency Cross-Check (COMB-01)'
.print '--------------------------------------------------------------------------'
.print 'Invariant: AutoEnsemble(mean, top_k=3) == (AutoARIMA + AutoETS + AutoTheta) / 3.0'
.print 'Condition: clean 60-obs linear series; all three Auto* members converge.'

-- Synthetic 60-observation linear series (single series, group id=1).
-- Linear trend y = 10 + 0.5*i ensures all three Auto* models fit reliably.
-- Zero nulls; moderate trend; no seasonality (seasonal_period=0).
CREATE OR REPLACE TABLE ae_test AS
SELECT
    1 AS id,
    '2020-01-01'::DATE + INTERVAL (i - 1) DAY AS ds,
    10.0 + i * 0.5 AS y
FROM generate_series(1, 60) t(i);

-- Step 1: AutoEnsemble with mean combination (top_k=3, non-seasonal).
CREATE OR REPLACE TABLE ae_mean AS
SELECT forecast_step, yhat AS ensemble_yhat, yhat_lower, yhat_upper, model_name
FROM ts_forecast_by(
    'ae_test', id, ds, y,
    'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'mean', seasonal_period: 0}
);

-- Step 2: Independent member forecasts (same horizon=5, non-seasonal).
CREATE OR REPLACE TABLE ae_arima AS
SELECT forecast_step, yhat AS y_arima
FROM ts_forecast_by(
    'ae_test', id, ds, y,
    'AutoARIMA', 5, '1d',
    params := {seasonal_period: 0}
);

CREATE OR REPLACE TABLE ae_ets AS
SELECT forecast_step, yhat AS y_ets
FROM ts_forecast_by(
    'ae_test', id, ds, y,
    'AutoETS', 5, '1d',
    params := {seasonal_period: 0}
);

CREATE OR REPLACE TABLE ae_theta AS
SELECT forecast_step, yhat AS y_theta
FROM ts_forecast_by(
    'ae_test', id, ds, y,
    'AutoTheta', 5, '1d',
    params := {seasonal_period: 0}
);

-- Step 3: Manual arithmetic mean of the three independent forecasts.
CREATE OR REPLACE TABLE ae_manual AS
SELECT
    a.forecast_step,
    (a.y_arima + e.y_ets + t.y_theta) / 3.0 AS manual_mean
FROM ae_arima a
JOIN ae_ets e USING (forecast_step)
JOIN ae_theta t USING (forecast_step);

.print ''
.print 'Cross-check: AutoEnsemble(mean) vs arithmetic mean of members'
.print 'All rows must show match=true (tolerance: 1e-6).'
SELECT
    m.forecast_step,
    ROUND(m.ensemble_yhat, 6) AS ensemble_yhat,
    ROUND(n.manual_mean, 6) AS manual_mean,
    abs(m.ensemble_yhat - n.manual_mean) AS diff,
    abs(m.ensemble_yhat - n.manual_mean) < 1e-6 AS match,
    m.yhat_lower,
    m.yhat_upper
FROM ae_mean m
JOIN ae_manual n USING (forecast_step)
ORDER BY m.forecast_step;

-- Assertion: fail loudly if any row does NOT match.
-- A non-empty result means the cross-check failed.
SELECT
    'CROSS-CHECK FAILED: AutoEnsemble(mean) != arithmetic mean of members' AS error_message,
    forecast_step,
    ensemble_yhat,
    manual_mean,
    abs(ensemble_yhat - manual_mean) AS diff
FROM (
    SELECT
        m.forecast_step,
        m.ensemble_yhat,
        n.manual_mean
    FROM ae_mean m
    JOIN ae_manual n USING (forecast_step)
) t
WHERE abs(ensemble_yhat - manual_mean) >= 1e-6;

-- ============================================================================
-- SECTION 2: AutoEnsemble Basic Usage
-- ============================================================================
-- Shows basic ts_forecast_by invocation with AutoEnsemble.
-- ============================================================================

.print ''
.print '>>> SECTION 2: Basic AutoEnsemble Usage'
.print '--------------------------------------------------------------------------'

.print 'AutoEnsemble with default params (top_k=3, combination_method=mean):'
SELECT
    forecast_step,
    ds,
    ROUND(yhat, 4) AS yhat,
    yhat_lower,
    yhat_upper,
    model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 5, '1d')
ORDER BY forecast_step;

-- ============================================================================
-- SECTION 3: Six-Method Smoke Test + Assertions (COMB-01..04)
-- ============================================================================
-- Verifies that ALL six combination_method strings produce finite non-NULL yhat
-- for every forecast step. Uses ae_test (60-obs linear series, already created).
--
-- COMB-01: mean, median
-- COMB-02: weighted_mse, inverse_aic
-- COMB-03: stacking
-- COMB-04: horizon_adaptive
-- ============================================================================

.print ''
.print '>>> SECTION 3: Six-Method Smoke Test with Assertions (COMB-01..04)'
.print '--------------------------------------------------------------------------'
.print 'All six combination methods must produce finite non-NULL yhat per step.'

-- Collect all six methods in one pass; tag each row with its method label.
CREATE OR REPLACE TABLE ae_smoke AS
-- COMB-01: mean (default) — unweighted arithmetic mean of member forecasts
SELECT 'mean' AS method, forecast_step, yhat, model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'mean', seasonal_period: 0})
UNION ALL
-- COMB-01: median — robust central tendency, less sensitive to outlier members
SELECT 'median', forecast_step, yhat, model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'median', seasonal_period: 0})
UNION ALL
-- COMB-02: weighted_mse — inverse-MSE weighting; better-fitting members get higher weight
SELECT 'weighted_mse', forecast_step, yhat, model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'weighted_mse', seasonal_period: 0})
UNION ALL
-- COMB-02: inverse_aic — AIC-based weighting; rewards model parsimony
SELECT 'inverse_aic', forecast_step, yhat, model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'inverse_aic', seasonal_period: 0})
UNION ALL
-- COMB-03: stacking — ridge-regularised stacking weights fit on in-sample holdout
SELECT 'stacking', forecast_step, yhat, model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'stacking', seasonal_period: 0})
UNION ALL
-- COMB-04: horizon_adaptive — per-horizon weights estimated from rolling-origin errors
SELECT 'horizon_adaptive', forecast_step, yhat, model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'horizon_adaptive', seasonal_period: 0});

.print ''
.print 'Smoke-test results (all ok must be true):'
SELECT
    method,
    forecast_step,
    ROUND(yhat, 4) AS yhat,
    yhat IS NOT NULL AND isfinite(yhat) AS ok
FROM ae_smoke
ORDER BY method, forecast_step;

-- Assertion: this query must return ZERO rows.
-- Any row here means a NULL or non-finite yhat slipped through.
SELECT
    'SMOKE-TEST FAILED: NULL or non-finite yhat for method=' || method
        || ' step=' || CAST(forecast_step AS VARCHAR) AS error_message,
    method,
    forecast_step,
    yhat
FROM ae_smoke
WHERE yhat IS NULL OR NOT isfinite(yhat);

-- ============================================================================
-- SECTION 4: Mean vs Median Demonstrability on a Skewed Series (COMB-01)
-- ============================================================================
-- Shows that Mean and Median produce VISIBLY DIFFERENT point forecasts when
-- the three member models diverge on a skewed series (exponential growth with
-- large outlier spikes). Mean pulls toward outlier values; Median stays
-- at the central member's forecast.
--
-- Assertion: at least one forecast_step must have abs(mean_yhat - median_yhat) > 1e-6.
-- ============================================================================

.print ''
.print '>>> SECTION 4: Mean vs Median Demonstrability — Skewed Series (COMB-01)'
.print '--------------------------------------------------------------------------'
.print 'Skewed series: exponential growth + outlier spikes at obs 10, 30, 50.'
.print 'Mean is pulled toward the high-value member; Median tracks the central one.'

-- Skewed series: exponential growth (exp(0.03*i)*10) with +200 spikes at i=10,30,50.
-- The spike pattern causes the three Auto* members to produce diverging extrapolations,
-- making Mean and Median visibly differ in the forecast horizon.
CREATE OR REPLACE TABLE ae_skew AS
SELECT
    1 AS id,
    '2020-01-01'::DATE + INTERVAL (i - 1) DAY AS ds,
    exp(i * 0.03) * 10.0 + CASE WHEN i IN (10, 30, 50) THEN 200.0 ELSE 0.0 END AS y
FROM generate_series(1, 60) t(i);

-- Mean combination on the skewed series.
CREATE OR REPLACE TABLE ae_skew_mean AS
SELECT forecast_step, yhat AS mean_yhat
FROM ts_forecast_by('ae_skew', id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'mean', seasonal_period: 0});

-- Median combination on the same skewed series.
CREATE OR REPLACE TABLE ae_skew_median AS
SELECT forecast_step, yhat AS median_yhat
FROM ts_forecast_by('ae_skew', id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'median', seasonal_period: 0});

.print ''
.print 'Mean vs Median comparison on skewed series:'
.print '(delta should be visibly non-zero for demonstrability)'
SELECT
    m.forecast_step,
    ROUND(m.mean_yhat, 4) AS mean_yhat,
    ROUND(n.median_yhat, 4) AS median_yhat,
    ROUND(ABS(m.mean_yhat - n.median_yhat), 4) AS delta
FROM ae_skew_mean m
JOIN ae_skew_median n USING (forecast_step)
ORDER BY m.forecast_step;

-- Assertion: Mean and Median MUST differ on at least one step.
-- An empty result here means the two methods returned identical forecasts,
-- which would indicate Mean == Median (fails COMB-01 demonstrability).
SELECT
    'DEMONSTRABILITY FAILED: Mean == Median on all steps (delta <= 1e-6 everywhere)' AS error_message
FROM (
    SELECT COUNT(*) AS n_different
    FROM ae_skew_mean m
    JOIN ae_skew_median n USING (forecast_step)
    WHERE ABS(m.mean_yhat - n.median_yhat) > 1e-6
) t
WHERE n_different = 0;

.print ''
.print '============================================================================='
.print 'AutoEnsemble example complete.'
.print '============================================================================='
