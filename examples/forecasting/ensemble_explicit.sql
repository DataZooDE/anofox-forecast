-- ============================================================================
-- Explicit-Member Ensemble Forecasting — Phase 5 (ENS-02)
-- ============================================================================
-- Full DoD example for ts_forecast_ensemble_by(): canonical Mean cross-check,
-- six combination method smoke test, 26-member allowlist sample, and error
-- path demonstrations.
--
-- ENS-02: User can produce an explicit-member ensemble forecast per series by
-- naming the member models and a combination method; the extension fits each
-- member and combines them.
--
-- Run: ./build/release/duckdb -unsigned < examples/forecasting/ensemble_explicit.sql
-- ============================================================================

LOAD anofox_forecast;

.print '============================================================================='
.print 'EXPLICIT-MEMBER ENSEMBLE FORECASTING — Phase 5 (ENS-02)'
.print '============================================================================='

-- ============================================================================
-- Shared series: clean 60-obs linearly-trended series
-- y = 10 + i*0.5, daily from 2020-01-01.
-- Linear trend ensures Auto* models converge deterministically (ARIMA(0,1,0)
-- or equivalent), making the Mean cross-check exact to machine precision.
-- ============================================================================
CREATE OR REPLACE TABLE ae_test AS
SELECT 1 AS id,
       '2020-01-01'::DATE + INTERVAL (i - 1) DAY AS ds,
       10.0 + i * 0.5 AS y
FROM range(1, 61) t(i);

-- ============================================================================
-- SECTION 1: DoD Mean Cross-Check — ['AutoARIMA','AutoETS','Theta'] (ENS-02)
-- ============================================================================
-- Invariant: ts_forecast_ensemble_by([...], 'mean') equals the arithmetic mean
-- of each member's independent ts_forecast_by forecast, within 1e-6 tolerance.
-- This is the canonical ENS-02 Definition of Done member set (from CONTEXT.md).
-- ============================================================================

.print ''
.print '>>> SECTION 1: DoD Mean Cross-Check — canonical member set (ENS-02)'
.print '------------------------------------------------------------------------'
.print 'Invariant: ts_forecast_ensemble_by([AutoARIMA,AutoETS,Theta], mean)'
.print '           == (AutoARIMA + AutoETS + Theta) / 3.0  within 1e-6'

-- Step 1: Explicit ensemble — combination_method='mean', non-seasonal
CREATE OR REPLACE TABLE ens_result AS
SELECT * FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    5, '1d',
    combination_method := 'mean',
    seasonal_period := 0
);

.print ''
.print 'Ensemble result (ts_forecast_ensemble_by):'
SELECT * FROM ens_result ORDER BY id, forecast_step;

-- Step 2: Independent member forecasts (same series, same seasonal_period)
CREATE OR REPLACE TABLE m_arima AS
    SELECT id, forecast_step, yhat AS y_arima
    FROM ts_forecast_by('ae_test', id, ds, y, 'AutoARIMA', 5, '1d');

CREATE OR REPLACE TABLE m_ets AS
    SELECT id, forecast_step, yhat AS y_ets
    FROM ts_forecast_by('ae_test', id, ds, y, 'AutoETS', 5, '1d');

CREATE OR REPLACE TABLE m_theta AS
    SELECT id, forecast_step, yhat AS y_theta
    FROM ts_forecast_by('ae_test', id, ds, y, 'Theta', 5, '1d');

-- Step 3: Manual arithmetic mean
CREATE OR REPLACE TABLE m_manual AS
SELECT id, forecast_step,
       (y_arima + y_ets + y_theta) / 3.0 AS manual_mean
FROM m_arima
JOIN m_ets   USING (id, forecast_step)
JOIN m_theta USING (id, forecast_step);

-- Step 4: Cross-check comparison (all rows must show match=true)
.print ''
.print 'Cross-check: ens_yhat vs arithmetic mean of members (all must be match=true):'
SELECT
    e.id,
    e.forecast_step,
    e.yhat          AS ens_mean,
    m.manual_mean,
    abs(e.yhat - m.manual_mean)          AS diff,
    abs(e.yhat - m.manual_mean) < 1e-6   AS match
FROM ens_result e
JOIN m_manual m USING (id, forecast_step)
ORDER BY e.id, e.forecast_step;

-- Assertion: must return 0
.print ''
.print 'Assertion — mismatch_count (must be 0):'
SELECT count(*) AS mismatch_count
FROM ens_result e
JOIN m_manual m USING (id, forecast_step)
WHERE abs(e.yhat - m.manual_mean) >= 1e-6;

-- Assert NULL intervals (point-only in Phase 5; EPI-01 deferred to Phase 6)
.print ''
.print 'Assertion — NULL intervals (must be 0):'
SELECT count(*) AS non_null_intervals
FROM ens_result
WHERE yhat_lower IS NOT NULL OR yhat_upper IS NOT NULL;

-- ============================================================================
-- SECTION 2: Six-Method Smoke Test (ENS-02 / COMB-01..04)
-- ============================================================================
-- Verifies all six combination_method strings produce finite non-NULL yhat
-- for every forecast step. Phase 5 delivers point forecasts only — yhat_lower
-- and yhat_upper remain NULL (Phase 6 EPI-01 defers ensemble intervals).
-- ============================================================================

.print ''
.print '>>> SECTION 2: Six-Method Smoke Test (all six combination_method strings)'
.print '------------------------------------------------------------------------'
.print 'All six methods must produce finite non-NULL yhat and NULL intervals.'

CREATE OR REPLACE TABLE smoke AS
-- mean (default) — unweighted arithmetic mean of member forecasts
SELECT 'mean' AS method, forecast_step, yhat, yhat_lower, yhat_upper
FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := 'mean', seasonal_period := 0)
UNION ALL
-- median — coordinate-wise median; robust to outlier members
SELECT 'median', forecast_step, yhat, yhat_lower, yhat_upper
FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := 'median', seasonal_period := 0)
UNION ALL
-- weighted_mse — inverse-MSE weighting; better-fitting members get higher weight
SELECT 'weighted_mse', forecast_step, yhat, yhat_lower, yhat_upper
FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := 'weighted_mse', seasonal_period := 0)
UNION ALL
-- inverse_aic — AIC-based weighting; rewards model parsimony
SELECT 'inverse_aic', forecast_step, yhat, yhat_lower, yhat_upper
FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := 'inverse_aic', seasonal_period := 0)
UNION ALL
-- stacking — ridge-regularised stacking weights from in-sample holdout
SELECT 'stacking', forecast_step, yhat, yhat_lower, yhat_upper
FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := 'stacking', seasonal_period := 0)
UNION ALL
-- horizon_adaptive — per-horizon weights from rolling-origin errors
SELECT 'horizon_adaptive', forecast_step, yhat, yhat_lower, yhat_upper
FROM ts_forecast_ensemble_by('ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := 'horizon_adaptive', seasonal_period := 0);

.print ''
.print 'Smoke-test results (ok must be true for every row):'
SELECT method, forecast_step, ROUND(yhat, 6) AS yhat,
       yhat IS NOT NULL AND isfinite(yhat) AS ok
FROM smoke
ORDER BY method, forecast_step;

-- Assertion: zero failures
.print ''
.print 'Assertion — NULL/non-finite failures (must be 0 rows):'
SELECT method, forecast_step, yhat
FROM smoke
WHERE yhat IS NULL OR NOT isfinite(yhat);

-- Assertion: all intervals NULL (point forecasts only in Phase 5)
.print ''
.print 'Assertion — non-NULL intervals (must be 0):'
SELECT count(*) AS non_null_intervals
FROM smoke
WHERE yhat_lower IS NOT NULL OR yhat_upper IS NOT NULL;

-- ============================================================================
-- SECTION 3: Member-Allowlist Sample (26-member set, per-tier coverage)
-- ============================================================================
-- Demonstrates that representative members from all three tiers are reachable
-- as ensemble members. The compiler-exhaustive build_forecaster factory (05-01)
-- guarantees all 26 compile; this section proves runtime reachability for a
-- representative sample.
--
-- Tier 1 — best DoD candidates: AutoARIMA, AutoETS, AutoTheta
-- Tier 2 — seasonal-aware: Theta, OptimizedTheta, DynamicTheta,
--           DynamicOptimizedTheta, HoltWinters, SeasonalES, SeasonalESOptimized,
--           SeasonalNaive, SeasonalWindowAverage, ETS, Kalman
-- Tier 3 — non-seasonal baselines: Naive, SES, SESOptimized, RandomWalkDrift,
--           Holt, SMA, CrostonClassic, CrostonOptimized, CrostonSBA,
--           ADIDA, IMAPA, TSB
-- ============================================================================

.print ''
.print '>>> SECTION 3: Member-Allowlist Sample (representative Tier 1/2/3 coverage)'
.print '------------------------------------------------------------------------'
.print 'Each sample group must return finite non-NULL yhat.'

-- Sample A: Tier 3 non-seasonal baselines (no seasonal_period needed)
.print ''
.print 'Sample A — Tier 3 baselines: [Naive, SES]'
CREATE OR REPLACE TABLE s3a AS
SELECT forecast_step, yhat FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['Naive', 'SES'],
    3, '1d',
    combination_method := 'mean', seasonal_period := 0);
SELECT forecast_step, ROUND(yhat, 4) AS yhat FROM s3a ORDER BY forecast_step;

-- Sample B: Tier 1 + Tier 2 mix (non-seasonal Theta family)
.print ''
.print 'Sample B — Tier 1 + Tier 2: [AutoARIMA, Theta, Holt]'
CREATE OR REPLACE TABLE s12 AS
SELECT forecast_step, yhat FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'Theta', 'Holt'],
    3, '1d',
    combination_method := 'mean', seasonal_period := 0);
SELECT forecast_step, ROUND(yhat, 4) AS yhat FROM s12 ORDER BY forecast_step;

-- Sample C: Intermittent demand Tier 3 models
.print ''
.print 'Sample C — Tier 3 intermittent: [CrostonClassic, ADIDA, IMAPA]'
CREATE OR REPLACE TABLE s3b AS
SELECT forecast_step, yhat FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['CrostonClassic', 'ADIDA', 'IMAPA'],
    3, '1d',
    combination_method := 'mean', seasonal_period := 0);
SELECT forecast_step, ROUND(yhat, 4) AS yhat FROM s3b ORDER BY forecast_step;

-- Sample D: Seasonal Tier 2 models (seasonal_period=12 required)
-- SeasonalNaive and HoltWinters both require a period > 1.
-- With seasonal_period=0 the build_forecaster falls back to period=12 for
-- seasonal models (documented Pitfall 5); use explicit seasonal_period := 12
-- to be unambiguous.
.print ''
.print 'Sample D — Tier 2 seasonal: [SeasonalNaive, HoltWinters] (seasonal_period=12)'
CREATE OR REPLACE TABLE sseas AS
SELECT forecast_step, yhat FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['SeasonalNaive', 'HoltWinters'],
    3, '1d',
    combination_method := 'mean', seasonal_period := 12);
SELECT forecast_step, ROUND(yhat, 4) AS yhat FROM sseas ORDER BY forecast_step;

-- Assertion: all samples return finite non-NULL yhat
.print ''
.print 'Assertion — failures per sample (must all be 0):'
SELECT 'SampleA' AS grp, count(*) AS fails FROM s3a WHERE yhat IS NULL OR NOT isfinite(yhat)
UNION ALL
SELECT 'SampleB', count(*) FROM s12 WHERE yhat IS NULL OR NOT isfinite(yhat)
UNION ALL
SELECT 'SampleC', count(*) FROM s3b WHERE yhat IS NULL OR NOT isfinite(yhat)
UNION ALL
SELECT 'SampleD', count(*) FROM sseas WHERE yhat IS NULL OR NOT isfinite(yhat);

-- ============================================================================
-- SECTION 4: Error Paths (Expected Errors — run each independently)
-- ============================================================================
-- The following statements are expected to raise errors. Because a raising
-- statement aborts a piped SQL script, they are placed at the END of the
-- example. Each is separated by a comment naming the expected error message.
--
-- To see the individual error messages, run each block in isolation:
--
--   ./build/release/duckdb -unsigned -c "LOAD 'path/to/anofox_forecast.duckdb_extension'; ..."
--
-- Expected error messages (verified against the built extension):
--
--   (a) Unknown member name 'NotAModel':
--       "Invalid parameter 'members' = 'NotAModel': unknown model name 'NotAModel';
--        use the same names as ts_forecast_by"
--
--   (b) Fewer than 2 members:
--       "ts_forecast_ensemble_by: at least 2 members are required. Got 1."
--
--   (c) Blocked model 'GARCH':
--       "Invalid parameter 'members' = 'GARCH': GARCH is not supported as an
--        ensemble member: Forecaster::predict() returns simulated innovations,
--        not level forecasts; use AutoARIMA or AutoETS instead"
--
-- Each error names the offending member and, for blocked models, suggests an
-- alternative. The error messages are emitted by the Rust core (InvalidParameter)
-- and surfaced to SQL as DuckDB InvalidInput exceptions.
-- ============================================================================

.print ''
.print '>>> SECTION 4: Error-Path Demonstrations (EXPECTED ERRORS)'
.print '------------------------------------------------------------------------'
.print 'Each statement below RAISES an error. These are the last statements'
.print 'in the script. Copy any individual statement to observe its error message.'
.print ''
.print 'The three expected errors are:'
.print '  (a) Unknown member name: mentions NotAModel'
.print "  (b) Fewer than 2 members: 'at least 2 members are required'"
.print '  (c) Blocked model GARCH: names GARCH and suggests AutoARIMA or AutoETS'
.print ''
.print '--- (a) Unknown member name: NotAModel ---'

-- EXPECTED ERROR (a): unknown member 'NotAModel'
-- Expected: "Invalid parameter 'members' = 'NotAModel': unknown model name
--            'NotAModel'; use the same names as ts_forecast_by"
SELECT * FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'NotAModel'],
    3, '1d');
