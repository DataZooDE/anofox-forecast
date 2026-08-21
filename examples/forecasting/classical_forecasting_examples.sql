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
