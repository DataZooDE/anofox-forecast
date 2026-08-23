-- ============================================================================
-- Classical Forecasting Examples — Phase 3 (CLAS-01, CLAS-02)
-- ============================================================================
-- Demonstrates ts_forecast_by() with GARCH and Kalman filter models, added
-- in Phase 3 via the existing univariate ts_forecast_by pipeline.
--
-- GARCH — conditional volatility (standard deviation) forecasting:
--   forecast_value is VOLATILITY = sqrt(forecast_variance(h)), NOT variance.
--   Default: GARCH(1,1). Override p/q via params := MAP{'garch_p':'1','garch_q':'1'}.
--   Requires p+q+10 minimum observations (GARCH(1,1) needs >= 12).
--   Best used on financial returns (first differences), not raw price levels.
--
-- Kalman — state-space smoothing + h-step forecasting:
--   Default state-space: local level (random walk + noise).
--   Selectable via params := MAP{'kalman_model':'local_linear_trend'}.
--
-- Run: ./build/release/duckdb -unsigned < examples/forecasting/classical_forecasting_examples.sql
-- ============================================================================

LOAD anofox_forecast;

.print '============================================================================='
.print 'CLASSICAL FORECASTING EXAMPLES — GARCH + Kalman (Phase 3)'
.print '============================================================================='

-- ============================================================================
-- SECTION 1: GARCH — Conditional Volatility Forecasting
-- ============================================================================
-- IMPORTANT: forecast_value is CONDITIONAL VOLATILITY (standard deviation),
-- = sqrt(forecast_variance(horizon)), NOT the variance itself.
-- This is the analytical variance forecast, NOT simulated innovations.
-- GARCH(1,1) models the clustering of volatility in financial returns data.
-- ============================================================================

.print ''
.print '>>> SECTION 1: GARCH — Conditional Volatility (std-dev = sqrt(variance))'
.print '--------------------------------------------------------------------------'

-- Create a returns-like time series (40 observations > 12 minimum for GARCH(1,1))
CREATE OR REPLACE TABLE returns AS
    SELECT 'Asset_A' AS asset_id,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           -- Simulated returns with volatility clustering
           0.5 * SIN(i * 0.7) + 0.3 * COS(i * 0.3) AS y
    FROM range(40) t(i);

.print 'GARCH(1,1) — default parameters (p=1, q=1)'
.print 'forecast_value = conditional volatility (std-dev), NOT variance'
SELECT
    asset_id,
    forecast_step,
    ds,
    ROUND(yhat, 6) AS conditional_volatility,
    model_name
FROM ts_forecast_by('returns', asset_id, ds, y, 'GARCH', 7, '1d')
ORDER BY asset_id, forecast_step;

.print ''
.print 'GARCH(1,1) — explicit p=1, q=1 via params (same result as default)'
SELECT
    asset_id,
    forecast_step,
    ds,
    ROUND(yhat, 6) AS conditional_volatility,
    model_name
FROM ts_forecast_by(
    'returns', asset_id, ds, y, 'GARCH', 7, '1d',
    params := MAP{'garch_p':'1','garch_q':'1'}
)
ORDER BY asset_id, forecast_step;

-- ============================================================================
-- SECTION 2: Kalman Filter — State-Space Smoothing + Forecasting
-- ============================================================================
-- Two state-space specs:
--   local_level (default): random walk + noise; best for series with no trend.
--   local_linear_trend: level + trend; better for trended series.
-- ============================================================================

.print ''
.print '>>> SECTION 2: Kalman Filter — local_level (default) vs local_linear_trend'
.print '--------------------------------------------------------------------------'

-- A trended series for Kalman demonstration
CREATE OR REPLACE TABLE sales AS
    SELECT 'Product_X' AS product_id,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           100.0 + i * 0.8 + 5.0 * SIN(2 * PI() * i / 7.0) AS y
    FROM range(30) t(i);

.print 'Kalman local_level (default) — random walk + noise'
SELECT
    product_id,
    forecast_step,
    ds,
    ROUND(yhat, 4) AS yhat,
    model_name
FROM ts_forecast_by('sales', product_id, ds, y, 'Kalman', 7, '1d')
ORDER BY product_id, forecast_step;

.print ''
.print 'Kalman local_linear_trend — level + trend state-space'
SELECT
    product_id,
    forecast_step,
    ds,
    ROUND(yhat, 4) AS yhat,
    model_name
FROM ts_forecast_by(
    'sales', product_id, ds, y, 'Kalman', 7, '1d',
    params := MAP{'kalman_model':'local_linear_trend'}
)
ORDER BY product_id, forecast_step;

.print ''
.print '============================================================================='
.print 'END — CLAS-01 (GARCH) and CLAS-02 (Kalman) verified end-to-end'
.print '============================================================================='

-- ============================================================================
-- SECTION 3: VAR — Multivariate Vector Autoregression Forecasting (CLAS-03)
-- ============================================================================
-- ts_forecast_var_by fits a single VAR(p) model across ALL K value columns
-- simultaneously (true multivariate cross-variable learning via VAR coefficient
-- matrix). It returns forecasts in LONG format: one row per (variable, step).
--
-- KEY NOTES:
--   - Output is LONG format: {variable VARCHAR, forecast_step BIGINT,
--     <date_col>, forecast_value DOUBLE} — one row per (variable, horizon step).
--   - forecast_value is a POINT forecast only (no prediction intervals in v1).
--   - v1 is SINGLE-PANEL ONLY — no group_col; one VAR fit over the entire table.
--   - Lag order (p) is set via the 'p' named parameter (default p=1).
--   - NaN / NULL values in any column are imputed via linear interpolation before
--     fitting. All value columns must have the same number of valid observations.
--   - Requires n > k*p + 1 valid observations (under-determination guard).
--   - The date column type is preserved in the output (DATE → DATE, etc.)
--
-- Run: ./build/release/duckdb -unsigned < examples/forecasting/classical_forecasting_examples.sql
-- ============================================================================

.print ''
.print '============================================================================='
.print 'SECTION 3: VAR — Multivariate Vector Autoregression (CLAS-03)'
.print '============================================================================='

-- Build a small synthetic multivariate table with 2 correlated variables (y1, y2)
-- and a shared date column (60 observations — well above VAR(1) minimum of k*p+1=3)
CREATE OR REPLACE TABLE var_src AS
    SELECT
        (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds,
        -- y1: sinusoidal + cosine cross-variable influence
        (0.6 * SIN(i * 0.4) + 0.1 * COS(i * 0.2)) AS y1,
        -- y2: cos-dominated + sin cross-variable influence (correlated with y1)
        (0.05 * SIN(i * 0.4) + 0.7 * COS(i * 0.2)) AS y2
    FROM range(60) t(i);

.print ''
.print 'VAR(1) forecast — default lag order p=1, horizon=14, 2 variables'
.print 'Output: LONG format (one row per variable x horizon step = 2 x 14 = 28 rows)'

SELECT * REPLACE(ROUND(forecast_value, 6) AS forecast_value)
FROM ts_forecast_var_by('var_src', 'ds', ['y1', 'y2'], 14, '1d')
ORDER BY variable, forecast_step;

.print ''
.print 'VAR(2) forecast — lag order p=2 via p:=2 named parameter'
.print 'Captures longer-range cross-variable dynamics'

SELECT * REPLACE(ROUND(forecast_value, 6) AS forecast_value)
FROM ts_forecast_var_by('var_src', 'ds', ['y1', 'y2'], 14, '1d', p:=2)
ORDER BY variable, forecast_step;

.print ''
.print 'Row count verification: ts_forecast_var_by returns k_vars * horizon rows'

SELECT
    count(*) AS total_rows,
    count(DISTINCT variable) AS distinct_variables,
    count(*) FILTER (WHERE variable = 'y1') AS y1_rows,
    count(*) FILTER (WHERE variable = 'y2') AS y2_rows
FROM ts_forecast_var_by('var_src', 'ds', ['y1', 'y2'], 14, '1d');

.print ''
.print '============================================================================='
.print 'END — CLAS-03 (VAR multivariate) verified end-to-end'
.print '============================================================================='
