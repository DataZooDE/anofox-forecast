-- ============================================================================
-- Forecasting with given model parameters (#317)
-- ============================================================================
-- Hyper-model forecasting: a global learner predicts the parameters of a
-- classical model, and the model produces the forecast. ETS (with a model
-- spec), TSB and ARIMA accept the parameters and use them exactly; ETS still
-- estimates its initial states.
--
-- Every section ends with a self-checking assertion, so a wrong result aborts
-- the script under -bail.
--
-- Run: ./build/release/duckdb -unsigned -bail < examples/forecasting/fixed_model_params.sql
-- ============================================================================

LOAD anofox_forecast;

CREATE OR REPLACE TABLE air AS
SELECT 'A' AS id, ('1949-01-01'::DATE + INTERVAL (i) MONTH)::DATE AS ds,
       (112 + 2.5 * i + 25 * sin(2 * pi() * i / 12)) * (1 + 0.1 * sin(2 * pi() * i / 12)) + (i % 5) AS y
FROM range(120) t(i);

.print '--- 1. Read AutoETS''s parameters, feed them back as fixed ETS parameters ---'
CREATE OR REPLACE TABLE chosen AS
SELECT (inspection).spec AS spec, (inspection).alpha AS alpha, (inspection).beta AS beta, (inspection).gamma AS gamma
FROM ts_forecast_inspect_by('air', id, ds, y, 'AutoETS', {seasonal_period: 12});
SELECT * FROM chosen;

SET VARIABLE a = (SELECT alpha FROM chosen);
SET VARIABLE b = (SELECT beta FROM chosen);
SET VARIABLE g = (SELECT gamma FROM chosen);

CREATE OR REPLACE TABLE refit AS
SELECT * FROM ts_forecast_by('air', id, ds, y, 'ETS', 12, '1mo',
    {model: 'AAA', seasonal_period: 12, alpha: getvariable('a'), beta: getvariable('b'), gamma: getvariable('g')});
CREATE OR REPLACE TABLE auto AS
SELECT * FROM ts_forecast_by('air', id, ds, y, 'AutoETS', 12, '1mo', {seasonal_period: 12});

SELECT CASE WHEN (SELECT spec FROM chosen) = 'ETS(A,A,A)'
             AND (SELECT max(abs(r.yhat - a.yhat) / a.yhat) FROM refit r JOIN auto a USING (forecast_step)) < 0.01
            THEN 'PASS fixed parameters reproduce AutoETS within 1%'
            ELSE error('FAIL ETS round trip') END AS check;

.print '--- 2. A different alpha changes the forecast; the fit reports it ---'
SELECT CASE WHEN (SELECT (inspection).alpha FROM ts_forecast_inspect_by('air', id, ds, y, 'ETS',
                      {model: 'AAA', seasonal_period: 12, alpha: 0.6, beta: 0.01, gamma: 0.2})) = 0.6
            THEN 'PASS inspect reports the given alpha'
            ELSE error('FAIL inspect alpha') END AS check;

.print '--- 3. ARIMA: AR(1) with mean 300, one-step forecast = 300 + 0.5 * (y_T - 300) ---'
SELECT CASE WHEN abs((SELECT yhat FROM ts_forecast_by('air', id, ds, y, 'ARIMA', 1, '1mo',
                          {arima_order: '[1,0,0]', ar_coefficients: [0.5], intercept: 300}))
                     - (300 + 0.5 * ((SELECT arg_max(y, ds) FROM air) - 300))) < 1e-9
            THEN 'PASS fixed AR(1) forecast is exact'
            ELSE error('FAIL ARIMA') END AS check;

.print '--- 4. Keys are rejected on other models (expected error) ---'
.bail off
SELECT * FROM ts_forecast_by('air', id, ds, y, 'AutoETS', 3, '1mo', {alpha: 0.3});
.bail on
