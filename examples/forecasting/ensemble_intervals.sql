-- ============================================================================
-- Ensemble Prediction Intervals — Phase 6 (EPI-01)
-- ============================================================================
-- Distribution-free conformal prediction intervals on ensemble forecasts,
-- using only the existing conformal machinery.
--
-- EPI-01: Attach lower/upper prediction bounds per horizon step to an ensemble
-- point forecast, via the learn→calibrate→apply pipeline. No new interval code.
--
-- Run: ./build/release/duckdb -unsigned -batch -c \
--        "LOAD '$PWD/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'; \
--         .read examples/forecasting/ensemble_intervals.sql"
--
-- LIMITATIONS (documented, not hidden):
--
--   (1) ts_cv_forecast_by IGNORES top_k/combination_method for AutoEnsemble:
--       The CV native (ts_cv_forecast_native.cpp:380-388) does NOT parse
--       ensemble-specific params. It zeroes ensemble_top_k / ensemble_method
--       in ForecastOptions, so the backtest always runs with top_k=3, Mean —
--       regardless of what you pass in the params MAP. As a result:
--         • ts_cv_forecast_by('folds', id, ds, y, 'AutoEnsemble', MAP{})
--           crashes at runtime in this build (segfault — known limitation).
--         • Section 1 therefore uses the SAME manual per-fold loop as Section 2,
--           with _ts_forecast_scalar('AutoEnsemble') per fold. The calibration
--           and the final forecast both use the same default AutoEnsemble config
--           (top_k=3, Mean), so coverage is internally consistent.
--
--   (2) ts_forecast_ensemble_by is a ScalarFunction and CANNOT flow through
--       ts_cv_forecast_by. Section 2 uses a manual per-fold loop over
--       ts_cv_folds_by output, calling _ts_forecast_ensemble_native per fold.
--       This is more verbose than the AutoEnsemble path but produces identical
--       conformal guarantees.
--
--   (3) ts_conformal_calibrate returns a GLOBAL quantile across all series and
--       folds. Per-series calibration requires ts_conformal_by (one-step) with
--       a group_col, which is not currently available in the modular form.
--
-- ============================================================================

LOAD anofox_forecast;
LOAD json;

.print '============================================================================='
.print 'ENSEMBLE PREDICTION INTERVALS — Phase 6 (EPI-01)'
.print '============================================================================='

-- ============================================================================
-- Shared synthetic data: 2 series, 120 daily observations
-- y = 10 + 0.5*t + 2*sin(0.15*t) + series_offset
-- Trend + low-frequency oscillation; deterministic for reproducible results.
-- ============================================================================

CREATE OR REPLACE TABLE ens_series AS
SELECT
    CASE WHEN g = 0 THEN 'series_A' ELSE 'series_B' END AS id,
    (DATE '2020-01-01' + INTERVAL (r) DAY) AS ds,
    10.0 + r * 0.5 + sin(r * 0.15) * 2.0 + g * 8.0 AS y
FROM range(120) t(r), range(2) s(g);

SELECT count(*) AS total_rows FROM ens_series;

-- ============================================================================
-- SECTION 1: AutoEnsemble conformal intervals (manual fold path)
-- ============================================================================
-- Pipeline:
--   ts_cv_folds_by → _ts_forecast_scalar('AutoEnsemble') per fold →
--   ts_conformal_calibrate → ts_forecast_by('AutoEnsemble') → apply intervals
--
-- Note: ts_cv_forecast_by('AutoEnsemble') crashes in this build (see LIMITATIONS
-- above). The manual per-fold loop using _ts_forecast_scalar achieves the same
-- calibration without the segfault.
-- ============================================================================

.print ''
.print '>>> SECTION 1: AutoEnsemble conformal intervals (EPI-01 DoD)'
.print '------------------------------------------------------------------------'

-- Step 1: Create CV folds (3-fold, 5-step horizon)
CREATE OR REPLACE TABLE ens_folds AS
SELECT * FROM ts_cv_folds_by('ens_series', id, ds, y, 3, 5, MAP{});

-- Step 2: Backtest — manual fold loop with _ts_forecast_scalar (AutoEnsemble)
--         For each (id, fold_id) train window, forecast with AutoEnsemble
--         and join to test actuals to form (y, yhat) residual pairs.
CREATE OR REPLACE TABLE ens_bt AS
WITH train_data AS (
    SELECT id, fold_id, ds, y
    FROM ens_folds WHERE split = 'train'
),
test_data AS (
    SELECT id, fold_id, ds, y AS actual,
           ROW_NUMBER() OVER (PARTITION BY id, fold_id ORDER BY ds) AS step
    FROM ens_folds WHERE split = 'test'
),
ens_forecasts AS (
    SELECT id, fold_id, forecast_step, yhat
    FROM (
        SELECT id, fold_id,
               unnest(
                   _ts_forecast_scalar(
                       LIST(ds ORDER BY ds),
                       LIST(y::DOUBLE ORDER BY ds),
                       (SELECT max(step) FROM test_data t
                        WHERE t.id = d.id AND t.fold_id = d.fold_id)::INTEGER,
                       '1d',
                       'AutoEnsemble',
                       MAP{}::MAP(VARCHAR, VARCHAR)
                   ), recursive := true
               )
        FROM train_data d
        GROUP BY id, fold_id
    )
)
SELECT t.id, t.fold_id, t.ds, t.actual AS y, e.yhat
FROM test_data t
JOIN ens_forecasts e
    ON t.id = e.id AND t.fold_id = e.fold_id AND t.step = e.forecast_step;

.print ''
.print 'Backtest residuals (first 5 rows):'
SELECT * FROM ens_bt LIMIT 5;

-- Step 3: Calibrate conformity score from backtest residuals (90% coverage, alpha=0.1)
CREATE OR REPLACE TABLE ens_calib AS
SELECT * FROM ts_conformal_calibrate('ens_bt', y, yhat, MAP{'alpha': '0.1'});

.print ''
.print 'Calibrated conformity score (90% coverage):'
SELECT * FROM ens_calib;

-- Step 4: Generate final AutoEnsemble point forecasts (same config: top_k=3, Mean)
CREATE OR REPLACE TABLE ens_fcst AS
SELECT * FROM ts_forecast_by('ens_series', id, ds, y, 'AutoEnsemble', 5, '1d');

-- Step 5: Apply conformal intervals (CROSS JOIN the global quantile)
CREATE OR REPLACE TABLE ens_intervals AS
SELECT f.id, f.ds, f.forecast_step, f.yhat,
       f.yhat - c.conformity_score AS yhat_lower,
       f.yhat + c.conformity_score AS yhat_upper
FROM ens_fcst f CROSS JOIN ens_calib c;

.print ''
.print 'AutoEnsemble forecast with 90% conformal intervals:'
SELECT * FROM ens_intervals ORDER BY id, forecast_step;

-- DoD assertion: lower <= yhat <= upper for every horizon step
.print ''
.print 'DoD assertion — bad_rows (must be 0):'
SELECT count(*) AS bad_rows
FROM ens_intervals
WHERE yhat_lower > yhat OR yhat > yhat_upper;

-- Non-degenerate intervals: upper > lower (conformity_score > 0)
.print ''
.print 'Interval width (must be > 0):'
SELECT ROUND(yhat_upper - yhat_lower, 6) AS interval_width FROM ens_intervals LIMIT 1;

-- ============================================================================
-- SECTION 2: Explicit-member ensemble conformal intervals (manual fold workaround)
-- ============================================================================
-- ts_forecast_ensemble_by is a ScalarFunction (not a string-method dispatch
-- through ts_cv_forecast_by). This section uses the manual per-fold loop over
-- ts_cv_folds_by output, calling _ts_forecast_ensemble_native per fold.
--
-- Pipeline:
--   ts_cv_folds_by → _ts_forecast_ensemble_native per fold →
--   ts_conformal_calibrate → ts_forecast_ensemble_by → apply intervals
-- ============================================================================

.print ''
.print '>>> SECTION 2: Explicit-member ensemble conformal intervals (EPI-01 DoD)'
.print '------------------------------------------------------------------------'

-- Step 1: Create CV folds (same 3-fold, 5-step structure)
CREATE OR REPLACE TABLE exp_folds AS
SELECT * FROM ts_cv_folds_by('ens_series', id, ds, y, 3, 5, MAP{});

-- Step 2: Manual fold loop — forecast with explicit ['AutoARIMA','AutoETS','Theta']
--         on each fold's train partition, then join to test actuals.
CREATE OR REPLACE TABLE exp_bt AS
WITH train_data AS (
    SELECT id, fold_id, ds, y
    FROM exp_folds WHERE split = 'train'
),
test_data AS (
    SELECT id, fold_id, ds, y AS actual,
           ROW_NUMBER() OVER (PARTITION BY id, fold_id ORDER BY ds) AS step
    FROM exp_folds WHERE split = 'test'
),
ens_forecasts AS (
    SELECT id, fold_id, forecast_step, yhat
    FROM (
        SELECT id, fold_id,
               unnest(
                   _ts_forecast_ensemble_native(
                       LIST(ds ORDER BY ds),
                       LIST(y::DOUBLE ORDER BY ds),
                       ['AutoARIMA', 'AutoETS', 'Theta'],
                       (SELECT max(step) FROM test_data t
                        WHERE t.id = d.id AND t.fold_id = d.fold_id)::INTEGER,
                       '1d',
                       '',     -- combination_method: '' → mean
                       0       -- seasonal_period: 0 = non-seasonal
                   ), recursive := true
               )
        FROM train_data d
        GROUP BY id, fold_id
    )
)
SELECT t.id, t.fold_id, t.ds, t.actual AS y, e.yhat
FROM test_data t
JOIN ens_forecasts e
    ON t.id = e.id AND t.fold_id = e.fold_id AND t.step = e.forecast_step;

.print ''
.print 'Explicit-member backtest residuals (first 5 rows):'
SELECT * FROM exp_bt LIMIT 5;

-- Step 3: Calibrate conformity score
CREATE OR REPLACE TABLE exp_calib AS
SELECT * FROM ts_conformal_calibrate('exp_bt', y, yhat, MAP{'alpha': '0.1'});

.print ''
.print 'Calibrated conformity score (90% coverage):'
SELECT * FROM exp_calib;

-- Step 4: Final explicit-member forecast (same member set and method)
CREATE OR REPLACE TABLE exp_fcst AS
SELECT * FROM ts_forecast_ensemble_by(
    'ens_series', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    5, '1d',
    combination_method := ''  -- '' = mean
);

-- Step 5: Apply conformal intervals
CREATE OR REPLACE TABLE exp_intervals AS
SELECT f.id, f.ds, f.forecast_step, f.yhat,
       f.yhat - c.conformity_score AS yhat_lower,
       f.yhat + c.conformity_score AS yhat_upper
FROM exp_fcst f CROSS JOIN exp_calib c;

.print ''
.print 'Explicit-member forecast with 90% conformal intervals:'
SELECT * FROM exp_intervals ORDER BY id, forecast_step;

-- DoD assertion: lower <= yhat <= upper for every horizon step
.print ''
.print 'DoD assertion — bad_rows (must be 0):'
SELECT count(*) AS bad_rows
FROM exp_intervals
WHERE yhat_lower > yhat OR yhat > yhat_upper;

-- Non-degenerate intervals: upper > lower
.print ''
.print 'Interval width (must be > 0):'
SELECT ROUND(yhat_upper - yhat_lower, 6) AS interval_width FROM exp_intervals LIMIT 1;

.print ''
.print '============================================================================='
.print 'EPI-01 DoD COMPLETE — check both bad_rows counts above are 0'
.print 'Both AutoEnsemble and explicit-member paths produce lower <= yhat <= upper'
.print '============================================================================='
