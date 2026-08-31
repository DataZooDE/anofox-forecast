-- ============================================================================
-- ensemble_explicit_tracer.sql — Phase 5 (ENS-02) tracer
-- ============================================================================
-- Verifies the explicit-member ensemble end-to-end on a 60-obs linearly-
-- trended series using ['AutoARIMA','AutoETS','Naive'] with combination_method
-- 'mean'. The central DoD cross-check proves:
--
--   ensemble_yhat == (AutoARIMA_yhat + AutoETS_yhat + Naive_yhat) / 3.0
--
-- within 1e-6 for every forecast step (mismatch_count must be 0).
--
-- Run: ./build/release/duckdb -unsigned -batch \
--        -c "LOAD '$PWD/build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'; \
--            .read examples/forecasting/ensemble_explicit_tracer.sql"
-- ============================================================================

LOAD anofox_forecast;

.print '============================================================='
.print 'Phase 5 (ENS-02): Explicit-Member Ensemble Tracer'
.print '============================================================='

-- ============================================================================
-- Build a clean 60-obs linearly-trended series (same shape as autoensemble.sql
-- so Auto* model selection is deterministic — ARIMA(0,1,0) on a trend series)
-- ============================================================================
CREATE OR REPLACE TABLE ae_test AS
SELECT 1 AS id,
       '2020-01-01'::DATE + INTERVAL (i - 1) DAY AS ds,
       10.0 + i * 0.5 AS y
FROM range(1, 61) t(i);

.print ''
.print '--- SECTION 1: DoD cross-check (Mean ensemble == arithmetic mean) ---'

-- Step 1: Ensemble forecast (combination_method := 'mean')
CREATE OR REPLACE TABLE ens_result AS
SELECT * FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Naive'],
    5, '1d',
    combination_method := 'mean',
    seasonal_period := 0
);

.print 'Ensemble result (ts_forecast_ensemble_by):'
SELECT * FROM ens_result ORDER BY id, forecast_step;

-- Step 2: Individual member forecasts (same series, same defaults)
CREATE OR REPLACE TABLE m_arima AS
    SELECT id, forecast_step, yhat AS y_arima
    FROM ts_forecast_by('ae_test', id, ds, y, 'AutoARIMA', 5, '1d');

CREATE OR REPLACE TABLE m_ets AS
    SELECT id, forecast_step, yhat AS y_ets
    FROM ts_forecast_by('ae_test', id, ds, y, 'AutoETS', 5, '1d');

CREATE OR REPLACE TABLE m_naive AS
    SELECT id, forecast_step, yhat AS y_naive
    FROM ts_forecast_by('ae_test', id, ds, y, 'Naive', 5, '1d');

-- Step 3: Manual arithmetic mean
CREATE OR REPLACE TABLE m_manual AS
SELECT id, forecast_step,
       (y_arima + y_ets + y_naive) / 3.0 AS manual_mean
FROM m_arima
JOIN m_ets   USING (id, forecast_step)
JOIN m_naive USING (id, forecast_step);

-- Step 4: Cross-check comparison
.print ''
.print 'Cross-check: ens_yhat vs arithmetic mean of members:'
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

-- Assertion: zero mismatches
.print ''
.print 'Assertion query (must return 0):'
SELECT count(*) AS mismatch_count
FROM ens_result e
JOIN m_manual m USING (id, forecast_step)
WHERE abs(e.yhat - m.manual_mean) >= 1e-6;

-- ============================================================================
-- Section 2: Verify yhat_lower / yhat_upper are NULL (point-only in Phase 5)
-- ============================================================================
.print ''
.print '--- SECTION 2: NULL intervals assertion ---'
SELECT count(*) AS non_null_intervals
FROM ens_result
WHERE yhat_lower IS NOT NULL OR yhat_upper IS NOT NULL;
-- Must return 0

.print ''
.print '--- SECTION 3: model_name check ---'
SELECT DISTINCT model_name FROM ens_result;
-- Must return 'Ensemble'

.print ''
.print '============================================================='
.print 'Tracer complete.'
.print '============================================================='
