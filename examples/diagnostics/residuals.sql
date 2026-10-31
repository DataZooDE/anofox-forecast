-- ============================================================================
-- Residual diagnostics (RESID-01..04) — anofox_forecast DuckDB extension
--
-- Run:
--   ./build/release/duckdb < examples/diagnostics/residuals.sql
--
-- Validate whether forecast residuals are "well-behaved":
--   ts_ljung_box            — residual autocorrelation (white-noise test)
--   ts_durbin_watson        — first-order autocorrelation statistic
--   ts_jarque_bera          — normality of residuals
--   ts_residual_diagnostics — combined adequacy report (Ljung-Box gate)
-- ============================================================================

LOAD './build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Two residual series: 'clean' (white-noise-like) and 'autocorr' (correlated)
CREATE OR REPLACE TABLE resids AS
SELECT 'clean' AS series_id, i AS ds, (hash(i) % 1000 / 1000.0 - 0.5) AS e FROM range(1, 200) t(i)
UNION ALL
SELECT 'autocorr', i, sum(hash(j) % 1000 / 1000.0 - 0.5) OVER (ORDER BY i) AS e
FROM range(1, 200) t(i), LATERAL (SELECT i AS j);

.print '--- Ljung-Box (RESID-01) per series ---'
SELECT series_id, (lb).statistic AS q_stat, (lb).p_value, (lb).lags
FROM ts_ljung_box_by('resids', series_id, ds, e) AS t(series_id, lb)
ORDER BY series_id;

.print '--- Durbin-Watson (RESID-02) per series ---'
SELECT series_id, (dw).statistic, (dw).interpretation, (dw).p_value, (dw).method
FROM ts_durbin_watson_by('resids', series_id, ds, e) AS t(series_id, dw)
ORDER BY series_id;

.print '--- Jarque-Bera (RESID-03) per series ---'
SELECT series_id, (jb).statistic, (jb).p_value, (jb).skewness, (jb).excess_kurtosis
FROM ts_jarque_bera_by('resids', series_id, ds, e) AS t(series_id, jb)
ORDER BY series_id;

.print '--- Combined adequacy report (RESID-04) per series ---'
SELECT series_id, (rd).lb_p_value, (rd).dw_interpretation, (rd).adequate
FROM ts_residual_diagnostics_by('resids', series_id, ds, e) AS t(series_id, rd)
ORDER BY series_id;

-- ============================================================================
-- fitted_params (D-06/DIAG-04): model-aware Ljung-Box degrees of freedom
--
-- ts_ljung_box's default df == lags treats the input as raw residuals (0
-- fitted parameters). If the residuals instead come from a fitted model --
-- e.g. an AR(1) with 1 estimated parameter -- pass fitted_params := 1 so
-- df = lags - fitted_params matches R Box.test(fitdf = 1), the convention
-- used when testing model residuals (as opposed to raw data).
-- ============================================================================

.print '--- ts_ljung_box fitted_params: df = lags - fitted_params ---'
SELECT series_id, (lb).p_value, (lb).df
FROM ts_ljung_box_by('resids', series_id, ds, e, lags := 10, fitted_params := 1) AS t(series_id, lb)
ORDER BY series_id;

SELECT series_id, (lb0).df AS df_fitted_params_0, (lb1).df AS df_fitted_params_1
FROM (
    SELECT series_id,
           ts_ljung_box(LIST(e ORDER BY ds), 10, 0) AS lb0,
           ts_ljung_box(LIST(e ORDER BY ds), 10, 1) AS lb1
    FROM resids
    GROUP BY series_id
) t
ORDER BY series_id;
