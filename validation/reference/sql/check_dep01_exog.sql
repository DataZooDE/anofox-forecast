-- DEP-01 at SQL level: ts_forecast_exog_by must reproduce the numpy lstsq
-- regression effect (validation/reference/fixtures/multiscale_ols)
-- through the built extension, on the 365-row history / 5-row future
-- design (section "typical") and a 15,330-row tiled version of the same
-- design (section "15k").
--
-- As of plan 10-04 (D-10), the "typical"/"15k" sections exercise the
-- CRATE's own OptimizedTheta-with-regressors fit end to end (regressors
-- attached via CalendarAnnotations, predicted with
-- predict_with_exog_intervals) — not the wrapper OLS-plus-residual-Theta
-- hack this plan deletes. The AutoARIMA section below is likewise now a
-- REAL crate AutoARIMA order search with the regressors attached (not the
-- fixed ARIMA stand-in it used to be), so it is informational only for a
-- different reason than before: the crate's own exog-aware order search
-- result is still not pinned to a specific tolerance here.
--
-- Run (from the repo root, after `make release` or `GEN=ninja make release`):
--   python3 -c "import resource,subprocess,sys;rc=subprocess.run(sys.argv[1:],stdin=open('validation/reference/sql/check_dep01_exog.sql')).returncode;print('MAXRSS_KB',resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss);sys.exit(rc)" ./build/release/duckdb -unsigned -bail
--
-- Fixtures used:
--   validation/reference/fixtures/multiscale_ols/exog_typical.csv  (365 history + 5 future rows)
--   validation/reference/fixtures/multiscale_ols/cases.json        (tolerance.sql_point_abs)
--
-- Tolerance: read from cases.json's tolerance.sql_point_abs at run time
-- (never hardcoded here) — it covers the residual-model component
-- ts_forecast_exog_by adds on top of the pure regression effect:
-- OptimizedTheta forecasts the OLS residuals with Theta, which is ~0 on
-- near-white-noise residuals but not exactly 0.
--
-- Prints DEP01_SQL_PASS typical and DEP01_SQL_PASS 15k on success; calls
-- error() with the row count and max abs diff on failure.

.bail on

INSTALL json;
LOAD json;

CREATE TABLE exog_rows AS
SELECT * FROM read_csv(
    'validation/reference/fixtures/multiscale_ols/exog_typical.csv',
    header = true,
    columns = {
        'split': 'VARCHAR',
        'sid': 'VARCHAR',
        'date': 'DATE',
        'x1': 'DOUBLE',
        'x2': 'DOUBLE',
        'x3': 'DOUBLE',
        'x4': 'DOUBLE',
        'y': 'DOUBLE',
        'expected_regression': 'DOUBLE'
    }
);

CREATE TABLE exog_hist AS
SELECT sid, date, x1, x2, x3, x4, y FROM exog_rows WHERE split = 'history';

CREATE TABLE exog_fut AS
SELECT sid, date, x1, x2, x3, x4, expected_regression FROM exog_rows WHERE split = 'future';

CREATE TABLE tol AS
SELECT json_extract(content, '$.tolerance.sql_point_abs')::DOUBLE AS sql_point_abs
FROM read_text('validation/reference/fixtures/multiscale_ols/cases.json');

-- === Section: typical (365-row history, 5-row future) ===

CREATE TABLE fc_typical AS
SELECT * FROM ts_forecast_exog_by(
    'exog_hist', sid, date, y, ['x1', 'x2', 'x3', 'x4'],
    'exog_fut', date, ['x1', 'x2', 'x3', 'x4'],
    '1d', 'OptimizedTheta', 5, MAP {}
);

SELECT
    f.forecast_step,
    f.yhat,
    e.expected_regression,
    abs(f.yhat - e.expected_regression) AS abs_diff
FROM fc_typical f
JOIN exog_fut e ON e.date = f.date::DATE
ORDER BY f.forecast_step;

SELECT
    CASE
        WHEN count(*) = 5
         AND max(abs(f.yhat - e.expected_regression)) <= (SELECT sql_point_abs FROM tol)
            THEN 'DEP01_SQL_PASS typical'
        ELSE error(
            'DEP01_SQL_FAIL typical: rows=' || count(*)::VARCHAR ||
            ' max_abs_diff=' || max(abs(f.yhat - e.expected_regression))::VARCHAR
        )
    END AS verdict
FROM fc_typical f
JOIN exog_fut e ON e.date = f.date::DATE;

-- Informational only, no assertion: AutoARIMA is ts_forecast_exog_by's
-- documented default. As of plan 10-04 it runs the crate's own exog-aware
-- AutoARIMA order search (CalendarAnnotations + predict_with_exog_intervals,
-- D-10) rather than the old fixed-ARIMA-on-OLS-residuals stand-in — but the
-- order AutoARIMA selects on this fixture is still not a value this check
-- pins to a tolerance, so it stays informational.
CREATE TABLE fc_typical_autoarima AS
SELECT * FROM ts_forecast_exog_by(
    'exog_hist', sid, date, y, ['x1', 'x2', 'x3', 'x4'],
    'exog_fut', date, ['x1', 'x2', 'x3', 'x4'],
    '1d', 'AutoARIMA', 5, MAP {}
);

SELECT
    'INFO AutoARIMA step1 yhat=' ||
    (SELECT f.yhat FROM fc_typical_autoarima f WHERE f.forecast_step = 1)::VARCHAR ||
    ' max_abs_diff=' ||
    (SELECT max(abs(f.yhat - e.expected_regression))
     FROM fc_typical_autoarima f JOIN exog_fut e ON e.date = f.date::DATE)::VARCHAR
    AS info;

-- === Section: 15k (15,330-row tiled history, same 5-row future) ===
-- Tiling the 365-row design 42x leaves the OLS coefficients unchanged
-- (X^T X and X^T y both scale linearly in the number of tiles), so the
-- same expected_regression column still applies.

CREATE TABLE exog_hist_15k AS
WITH numbered AS (
    SELECT sid, x1, x2, x3, x4, y, (ROW_NUMBER() OVER (ORDER BY date) - 1) AS i
    FROM exog_hist
),
tiled AS (
    SELECT sid, x1, x2, x3, x4, y, i, k
    FROM numbered CROSS JOIN range(42) AS t (k)
)
SELECT
    sid,
    (DATE '2024-12-30' - ((15329 - (k * 365 + i)) * INTERVAL 1 DAY))::DATE AS date,
    x1, x2, x3, x4, y
FROM tiled;

SELECT
    CASE
        WHEN (SELECT count(*) FROM exog_hist_15k) = 15330
         AND (SELECT max(date) FROM exog_hist_15k) = DATE '2024-12-30'
            THEN 'OK'
        ELSE error(
            'DEP01_SQL_FAIL 15k setup: count=' ||
            (SELECT count(*) FROM exog_hist_15k)::VARCHAR ||
            ' max(date)=' || (SELECT max(date) FROM exog_hist_15k)::VARCHAR
        )
    END AS setup_check;

CREATE TABLE fc_15k AS
SELECT * FROM ts_forecast_exog_by(
    'exog_hist_15k', sid, date, y, ['x1', 'x2', 'x3', 'x4'],
    'exog_fut', date, ['x1', 'x2', 'x3', 'x4'],
    '1d', 'OptimizedTheta', 5, MAP {}
);

SELECT
    f.forecast_step,
    f.yhat,
    e.expected_regression,
    abs(f.yhat - e.expected_regression) AS abs_diff
FROM fc_15k f
JOIN exog_fut e ON e.date = f.date::DATE
ORDER BY f.forecast_step;

SELECT
    CASE
        WHEN count(*) = 5
         AND max(abs(f.yhat - e.expected_regression)) <= (SELECT sql_point_abs FROM tol)
            THEN 'DEP01_SQL_PASS 15k'
        ELSE error(
            'DEP01_SQL_FAIL 15k: rows=' || count(*)::VARCHAR ||
            ' max_abs_diff=' || max(abs(f.yhat - e.expected_regression))::VARCHAR
        )
    END AS verdict
FROM fc_15k f
JOIN exog_fut e ON e.date = f.date::DATE;
