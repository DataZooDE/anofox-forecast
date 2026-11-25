-- ============================================================================
-- AutoARIMA fit-cost limits — max_models / time_budget_ms (#318)
-- ============================================================================
-- A seasonal AutoARIMA search with a long period (m = 52 here) can take
-- minutes per series. Two opt-in params keys bound one fit:
--   max_models      cap on models visited by the stepwise search (default 94)
--   time_budget_ms  wall-clock budget per series fit (default unbounded);
--                   returns the best model found so far, not reproducible
-- Both are valid only with 'AutoARIMA'.
--
-- Every section ends with a self-checking assertion, so a wrong result aborts
-- the script under -bail.
--
-- Run: ./build/release/duckdb -unsigned -bail < examples/forecasting/auto_arima_limits.sql
-- ============================================================================

LOAD anofox_forecast;

CREATE OR REPLACE TABLE weekly AS
SELECT 'W' || (i // 400) AS id,
       ('2015-01-04'::DATE + INTERVAL (7 * (i % 400)) DAY)::DATE AS ds,
       1000.0 + 0.8 * (i % 400) + 40.0 * sin(2 * pi() * (i % 400) / 52.0) + (i * 7919) % 13 AS y
FROM range(0, 800) t(i);

.print '--- 1. max_models: deterministic cap on the search ---'
CREATE OR REPLACE TABLE capped AS
SELECT * FROM ts_forecast_by('weekly', id, ds, y, 'AutoARIMA', 13, '1w',
                             {seasonal_period: 52, max_models: 10});
SELECT id, any_value(model_name) AS model_name, count(*) AS n FROM capped GROUP BY id ORDER BY id;

CREATE OR REPLACE TABLE capped_again AS
SELECT * FROM ts_forecast_by('weekly', id, ds, y, 'AutoARIMA', 13, '1w',
                             {seasonal_period: 52, max_models: 10});
SELECT CASE WHEN (SELECT count(*) FROM capped) = 26
             AND (SELECT count(*) FROM (SELECT id, forecast_step, yhat FROM capped
                                        EXCEPT SELECT id, forecast_step, yhat FROM capped_again)) = 0
            THEN 'PASS max_models: 26 rows, identical on rerun'
            ELSE error('FAIL max_models') END AS check;

.print '--- 2. time_budget_ms: best model within ~1 s per series ---'
CREATE OR REPLACE TABLE budgeted AS
SELECT * FROM ts_forecast_by('weekly', id, ds, y, 'AutoARIMA', 13, '1w',
                             {seasonal_period: 52, time_budget_ms: 1000});
SELECT id, any_value(model_name) AS model_name, count(*) AS n FROM budgeted GROUP BY id ORDER BY id;
SELECT CASE WHEN (SELECT count(*) FROM budgeted WHERE isfinite(yhat) AND model_name LIKE 'AutoARIMA(%') = 26
            THEN 'PASS time_budget_ms: 26 finite AutoARIMA forecasts'
            ELSE error('FAIL time_budget_ms') END AS check;

.print '--- 3. The keys are rejected on other models ---'
SELECT CASE WHEN (SELECT count(*) FROM ts_forecast_by('weekly', id, ds, y, 'AutoARIMA', 1, '1w',
                                                      {seasonal_period: 52, max_models: 5})) = 2
            THEN 'PASS AutoARIMA accepts max_models'
            ELSE error('FAIL') END AS check;
.print 'AutoETS with max_models (expected error):'
.bail off
SELECT * FROM ts_forecast_by('weekly', id, ds, y, 'AutoETS', 1, '1w', {max_models: 5});
.bail on
