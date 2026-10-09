-- ============================================================================
-- Crate Delegation Examples — Phase 10 (FCST-01/04/05/06/07/08)
-- ============================================================================
-- Proves, against the BUILT extension, that the forecast surface now returns
-- the crate model's own output end to end: real prediction intervals (no
-- fabricated point ± z*sd(series)*sqrt(h) band), real crate ARIMA via
-- arima_order/seasonal_order, 'ETS' = AutoETS selection (standalone ==
-- ensemble member), AutoARIMA running its own order search WITH regressors,
-- and D-11/D-12 trailing-NULL input handling.
--
-- Every section ends with a self-checking assertion of the form
--   SELECT CASE WHEN <condition> THEN 'PASS <label>' ELSE error('FAIL ...') END;
-- so a wrong result aborts the whole script under -bail instead of silently
-- printing a bad number.
--
-- Run: ./build/release/duckdb -unsigned -bail < examples/forecasting/crate_delegation_examples.sql
-- ============================================================================

LOAD anofox_forecast;
INSTALL json;
LOAD json;

.print '============================================================================='
.print 'CRATE DELEGATION EXAMPLES — Phase 10 (FCST-01/04/05/06/07/08)'
.print '============================================================================='

-- ============================================================================
-- Shared fixture: AirPassengers (144 monthly values, 1949-01 .. 1960-12)
-- validation/reference/fixtures/r_forecast/air_passengers.json
-- ============================================================================
CREATE OR REPLACE TABLE air AS
SELECT
    'A' AS id,
    ('1949-01-01'::DATE + (i || ' months')::INTERVAL)::DATE AS ds,
    y::DOUBLE AS y
FROM (
    SELECT unnest(range(144)) AS i,
           unnest([112, 118, 132, 129, 121, 135, 148, 148, 136, 119, 104, 118,
                   115, 126, 141, 135, 125, 149, 170, 170, 158, 133, 114, 140,
                   145, 150, 178, 163, 172, 178, 199, 199, 184, 162, 146, 166,
                   171, 180, 193, 181, 183, 218, 230, 242, 209, 191, 172, 194,
                   196, 196, 236, 235, 229, 243, 264, 272, 237, 211, 180, 201,
                   204, 188, 235, 227, 234, 264, 302, 293, 259, 229, 203, 229,
                   242, 233, 267, 269, 270, 315, 364, 347, 312, 274, 237, 278,
                   284, 277, 317, 313, 318, 374, 413, 405, 355, 306, 271, 306,
                   315, 301, 356, 348, 355, 422, 465, 467, 404, 347, 305, 336,
                   340, 318, 362, 348, 363, 435, 491, 505, 404, 359, 310, 337,
                   360, 342, 406, 396, 420, 472, 548, 559, 463, 407, 362, 405,
                   417, 391, 419, 461, 472, 535, 622, 606, 508, 461, 390, 432]) AS y
);

-- ============================================================================
-- SECTION 1: Crate-native prediction intervals (D-01/D-03)
-- ============================================================================
-- The crate's own Naive interval half-width at step 1 — not the old fabricated
-- point ± z*sd(series)*sqrt(h) band (which produced a ~234 half-width here).
-- The crate computes z from a private Abramowitz & Stegun 26.2.23
-- approximation (not an exact Φ⁻¹ table lookup), so this is asserted at 0.05
-- absolute vs the R naive() reference (66.07118560930098) — two orders of
-- magnitude tighter than the ~168 gap the old fabrication would show — and the
-- 0.80/0.95 half-width ratio is asserted at 1e-3 vs the exact Φ⁻¹(0.90)/Φ⁻¹(0.975)
-- ratio (0.6538648544837132), the same precedent set in
-- test/sql/ts_forecast_crate_delegation.test.
-- ============================================================================

.print ''
.print '>>> SECTION 1: Crate-native prediction intervals (D-01/D-03)'
.print '-----------------------------------------------------------------------------'

CREATE OR REPLACE TABLE naive_hw AS
SELECT 0.95 AS confidence_level, (yhat_upper - yhat) AS half_width
FROM ts_forecast_by('air', id, ds, y, 'Naive', 1, '1mo', MAP{'confidence_level': '0.95'})
UNION ALL
SELECT 0.80 AS confidence_level, (yhat_upper - yhat) AS half_width
FROM ts_forecast_by('air', id, ds, y, 'Naive', 1, '1mo', MAP{'confidence_level': '0.80'});

.print 'Naive half-widths at 95% and 80% confidence:'
SELECT * FROM naive_hw ORDER BY confidence_level;

SELECT CASE WHEN (
    SELECT abs(half_width - 66.07118560930098) < 0.05 FROM naive_hw WHERE confidence_level = 0.95
) THEN 'PASS naive_95_half_width' ELSE error(
    'FAIL naive_95_half_width: got ' || (SELECT half_width FROM naive_hw WHERE confidence_level = 0.95)::VARCHAR
) END AS result;

SELECT CASE WHEN (
    SELECT abs(
        (SELECT half_width FROM naive_hw WHERE confidence_level = 0.80)
        / (SELECT half_width FROM naive_hw WHERE confidence_level = 0.95)
        - 0.6538648544837132
    ) < 1e-3
) THEN 'PASS naive_80_95_ratio' ELSE error(
    'FAIL naive_80_95_ratio: got ' || (
        (SELECT half_width FROM naive_hw WHERE confidence_level = 0.80)
        / (SELECT half_width FROM naive_hw WHERE confidence_level = 0.95)
    )::VARCHAR
) END AS result;

-- ============================================================================
-- SECTION 2: Real crate ARIMA via arima_order/seasonal_order (D-08/D-09)
-- ============================================================================
-- 'ARIMA' without arima_order is a hard SQL error (not executed here, -bail
-- would abort the script):
--
--   SELECT * FROM ts_forecast_by('air', id, ds, y, 'ARIMA', 12, '1mo', MAP{});
--   -- Invalid Input Error: ARIMA requires arima_order=[p,d,q] (e.g. [1,1,1]);
--   --                       use 'AutoARIMA' for automatic order selection
--
-- With an explicit order, ARIMA runs the crate's own fit and reports the
-- order in model_name: ARIMA(0,1,1)(0,1,1)[12] matches R's
-- Arima(order=c(0,1,1), seasonal=c(0,1,1)) at the AirPassengers airline model
-- within 1% (validation/reference/fixtures/r_forecast/air_passengers.json).
-- ============================================================================

.print ''
.print '>>> SECTION 2: Real crate ARIMA via arima_order/seasonal_order (D-08/D-09)'
.print '-----------------------------------------------------------------------------'

CREATE OR REPLACE TABLE arima_step1 AS
SELECT model_name, yhat
FROM ts_forecast_by(
    'air', id, ds, y, 'ARIMA', 12, '1mo',
    MAP{'arima_order': '[0,1,1]', 'seasonal_order': '[0,1,1]', 'seasonal_period': '12'}
)
WHERE forecast_step = 1;

.print 'ARIMA(0,1,1)(0,1,1)[12] step-1 forecast:'
SELECT * FROM arima_step1;

SELECT CASE WHEN (
    SELECT model_name = 'ARIMA(0,1,1)(0,1,1)[12]' FROM arima_step1
) THEN 'PASS arima_model_name' ELSE error(
    'FAIL arima_model_name: got ' || (SELECT model_name FROM arima_step1)
) END AS result;

SELECT CASE WHEN (
    SELECT abs(yhat - 447.053211074412) / 447.053211074412 < 0.01 FROM arima_step1
) THEN 'PASS arima_step1_within_1pct_of_r' ELSE error(
    'FAIL arima_step1_within_1pct_of_r: got ' || (SELECT yhat FROM arima_step1)::VARCHAR
) END AS result;

-- ============================================================================
-- SECTION 3: 'ETS' = AutoETS, standalone == ensemble member (D-05/FCST-07)
-- ============================================================================
-- 'ETS' without ets_model runs the crate's AutoETS selection and reports the
-- chosen spec (e.g. ETS(A,N,M)); an explicit ['ETS', 'ETS'] ensemble (Mean)
-- must reproduce the standalone point exactly at every step, and the
-- ensemble's own prediction interval must be non-NULL.
-- ============================================================================

.print ''
.print '>>> SECTION 3: ''ETS'' = AutoETS selection, standalone == ensemble member (D-05/FCST-07)'
.print '-----------------------------------------------------------------------------'

CREATE OR REPLACE TABLE ets_standalone AS
SELECT forecast_step, yhat, model_name
FROM ts_forecast_by('air', id, ds, y, 'ETS', 12, '1mo', MAP{'seasonal_period': '12'});

.print 'Standalone ETS model_name (AutoETS selection):'
SELECT DISTINCT model_name FROM ets_standalone;

CREATE OR REPLACE TABLE ets_ensemble AS
SELECT forecast_step, yhat, yhat_lower, yhat_upper
FROM ts_forecast_ensemble_by(
    'air', id, ds, y, ['ETS', 'ETS'], 12, '1mo',
    combination_method := 'mean', seasonal_period := 12, confidence_level := 0.95
);

SELECT CASE WHEN (
    SELECT DISTINCT regexp_matches(model_name, '^ETS\([AM],(N|A|Ad),(N|A|M)\)$') FROM ets_standalone
) THEN 'PASS ets_model_name_pattern' ELSE error('FAIL ets_model_name_pattern') END AS result;

SELECT CASE WHEN (
    SELECT count(*) FROM ets_standalone s JOIN ets_ensemble e USING (forecast_step)
    WHERE abs(s.yhat - e.yhat) > 1e-9
) = 0 THEN 'PASS ets_ensemble_equals_standalone' ELSE error(
    'FAIL ets_ensemble_equals_standalone: ' || (
        SELECT count(*) FROM ets_standalone s JOIN ets_ensemble e USING (forecast_step)
        WHERE abs(s.yhat - e.yhat) > 1e-9
    )::VARCHAR || ' mismatched steps'
) END AS result;

SELECT CASE WHEN (
    SELECT count(*) FROM ets_ensemble WHERE yhat_lower IS NULL OR yhat_upper IS NULL
) = 0 THEN 'PASS ets_ensemble_bounds_non_null' ELSE error('FAIL ets_ensemble_bounds_non_null') END AS result;

-- ============================================================================
-- SECTION 4: Exogenous regressors via the crate's own exog support (D-10/FCST-05)
-- ============================================================================
-- AutoARIMA with regressors runs its real order search WITH the regressors
-- attached, including a holiday column that is 0 throughout history and 1 in
-- the future (never observed as 1 during fitting) — model_name reports the
-- selected order followed by " with regressors" (never a fixed "ARIMAX"
-- stand-in), and every forecasted point is finite.
-- ============================================================================

.print ''
.print '>>> SECTION 4: Exogenous regressors via the crate''s own exog support (D-10/FCST-05)'
.print '-----------------------------------------------------------------------------'

CREATE OR REPLACE TABLE exog_hist AS
SELECT 'S1' AS id,
       ('2020-01-01'::DATE + (i || ' days')::INTERVAL)::DATE AS ds,
       50.0 + 3.0 * sin(i * 0.37) + (i % 5)::DOUBLE AS y,
       sin(i * 0.37) AS x1,
       0.0 AS holiday
FROM range(120) AS t(i);

CREATE OR REPLACE TABLE exog_future AS
SELECT 'S1' AS id,
       ('2020-01-01'::DATE + ((120 + i) || ' days')::INTERVAL)::DATE AS ds,
       sin((120 + i) * 0.37) AS x1,
       1.0 AS holiday
FROM range(6) AS t(i);

CREATE OR REPLACE TABLE exog_result AS
SELECT model_name, forecast_step, yhat
FROM ts_forecast_exog_by(
    'exog_hist', id, ds, y, ['x1', 'holiday'],
    'exog_future', ds, ['x1', 'holiday'],
    '1d', 'AutoARIMA', 6, MAP{}
);

.print 'AutoARIMA with regressors forecast:'
SELECT * FROM exog_result ORDER BY forecast_step;

SELECT CASE WHEN (
    SELECT DISTINCT regexp_matches(model_name, '^AutoARIMA\(\d+,\d+,\d+\)(\(\d+,\d+,\d+\)\[\d+\])? with regressors$') FROM exog_result
) THEN 'PASS exog_autoarima_model_name' ELSE error(
    'FAIL exog_autoarima_model_name: got ' || (SELECT DISTINCT model_name FROM exog_result)
) END AS result;

SELECT CASE WHEN (
    SELECT count(*) FROM exog_result WHERE yhat IS NULL OR NOT isfinite(yhat)
) = 0 THEN 'PASS exog_all_finite' ELSE error('FAIL exog_all_finite') END AS result;

-- ============================================================================
-- SECTION 5: Input handling — trailing-NULL realignment (D-11/D-12/FCST-08)
-- ============================================================================
-- A daily series y = 0..29 followed by two trailing NULL rows. Leading/
-- trailing NULLs are trimmed before fitting, but forecast_step=1 realigns to
-- the period AFTER the ORIGINAL last row (not the last valid row) —
-- RandomWalkDrift's drift-projected value at that date must be exactly 32.
-- ============================================================================

.print ''
.print '>>> SECTION 5: Input handling — trailing-NULL realignment (D-11/D-12/FCST-08)'
.print '-----------------------------------------------------------------------------'

CREATE OR REPLACE TABLE rwd_trailing_null AS
SELECT 'A' AS id,
       ('2020-01-01'::DATE + (i || ' days')::INTERVAL)::DATE AS ds,
       CASE WHEN i >= 30 THEN NULL ELSE i::DOUBLE END AS y
FROM range(32) AS t(i);

CREATE OR REPLACE TABLE rwd_step1 AS
SELECT ds, yhat
FROM ts_forecast_by('rwd_trailing_null', id, ds, y, 'RandomWalkDrift', 3, '1d', MAP{})
WHERE forecast_step = 1;

.print 'RandomWalkDrift step-1 forecast after two trailing NULL rows:'
SELECT * FROM rwd_step1;

SELECT CASE WHEN (
    SELECT yhat = 32.0 AND ds = DATE '2020-02-02' FROM rwd_step1
) THEN 'PASS rwd_trailing_null_realignment' ELSE error(
    'FAIL rwd_trailing_null_realignment: got yhat=' || (SELECT yhat FROM rwd_step1)::VARCHAR
    || ' ds=' || (SELECT ds FROM rwd_step1)::VARCHAR
) END AS result;

.print ''
.print '============================================================================='
.print 'ALL SECTIONS PASSED'
.print '============================================================================='
