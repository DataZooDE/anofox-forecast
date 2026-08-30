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
-- SECTION 3: Combination Method Smoke Test
-- ============================================================================
-- Verifies that all six combination_method values produce finite non-NULL yhat.
-- ============================================================================

.print ''
.print '>>> SECTION 3: All Six Combination Methods (smoke test — each must return finite yhat)'
.print '--------------------------------------------------------------------------'

.print 'mean (default):'
SELECT forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 3, '1d',
    params := {combination_method: 'mean'})
ORDER BY forecast_step;

.print ''
.print 'median:'
SELECT forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 3, '1d',
    params := {combination_method: 'median'})
ORDER BY forecast_step;

.print ''
.print 'weighted_mse:'
SELECT forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 3, '1d',
    params := {combination_method: 'weighted_mse'})
ORDER BY forecast_step;

.print ''
.print 'inverse_aic:'
SELECT forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 3, '1d',
    params := {combination_method: 'inverse_aic'})
ORDER BY forecast_step;

.print ''
.print 'stacking:'
SELECT forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 3, '1d',
    params := {combination_method: 'stacking'})
ORDER BY forecast_step;

.print ''
.print 'horizon_adaptive:'
SELECT forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 3, '1d',
    params := {combination_method: 'horizon_adaptive'})
ORDER BY forecast_step;

.print ''
.print '============================================================================='
.print 'AutoEnsemble example complete.'
.print '============================================================================='
