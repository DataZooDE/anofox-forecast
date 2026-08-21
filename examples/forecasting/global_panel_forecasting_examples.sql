-- ============================================================================
-- Global Panel Forecasting Examples (Phase 2: GLOB-01..03)
-- ============================================================================
-- Demonstrates ts_forecast_panel_by() with GlobalETS — a cross-series global
-- learner that fits shared exponential-smoothing parameters across all panel
-- members simultaneously (fit-once-emit-many pattern).
--
-- Unlike ts_forecast_by (per-series independent fits), the global model shares
-- parameters across the panel, which is particularly effective when individual
-- series are short but collectively form a large, homogeneous dataset.
--
-- Run: ./build/release/duckdb -unsigned < examples/forecasting/global_panel_forecasting_examples.sql
-- ============================================================================

LOAD anofox_forecast;

.print '============================================================================='
.print 'GLOBAL PANEL FORECASTING — ts_forecast_panel_by()'
.print '============================================================================='

-- ============================================================================
-- SECTION 1: Ragged Panel — GlobalETS fit-once-emit-many
-- ============================================================================
-- Three series of different lengths (ragged panel). The function:
--   1. Aligns all series to a shared date grid (union of dates, NaN for gaps)
--   2. Drops series with < 10 valid observations (surfaced as DROPPED rows)
--   3. Makes one GlobalETS fit across all aligned series
--   4. Returns horizon forecast rows per kept series

.print ''
.print '>>> SECTION 1: Ragged Panel — GlobalETS (non-seasonal)'
.print '--------------------------------------------------------------------------'

CREATE OR REPLACE TABLE panel AS
    -- Series A: 30 daily observations starting 2024-01-01
    SELECT 'Product_A' AS product_id,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           100.0 + i * 0.5 + 12.0 * SIN(2 * PI() * i / 7.0) AS y
    FROM generate_series(0, 29) t(i)
    UNION ALL
    -- Series B: 25 daily observations starting 2024-01-01 (ragged end)
    SELECT 'Product_B',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           80.0 + i * 0.3 + 8.0 * COS(2 * PI() * i / 7.0)
    FROM generate_series(0, 24) t(i)
    UNION ALL
    -- Series C: 20 daily observations starting 2024-01-05 (ragged start AND end)
    SELECT 'Product_C',
           DATE '2024-01-05' + INTERVAL (i) DAY,
           60.0 + i * 0.8 + 5.0 * SIN(2 * PI() * i / 7.0 + 1.0)
    FROM generate_series(0, 19) t(i);

.print 'Panel series lengths:'
SELECT product_id, count(*) AS n_obs, min(ds) AS first_date, max(ds) AS last_date
FROM panel
GROUP BY product_id
ORDER BY product_id;

.print ''
.print 'GlobalETS panel forecast (horizon=14, frequency=1d, non-seasonal):'
CREATE OR REPLACE TABLE panel_forecasts AS
SELECT *
FROM ts_forecast_panel_by(
    'panel',
    product_id,
    ds,
    y,
    'GlobalETS',
    14,
    '1d'
);

SELECT product_id, forecast_step, ds, ROUND(yhat, 2) AS yhat, model_name
FROM panel_forecasts
ORDER BY product_id, forecast_step
LIMIT 15;

.print ''
.print 'Forecast count check (expect 14 rows per series, 42 total):'
SELECT product_id, count(*) AS n_forecasts
FROM panel_forecasts
GROUP BY product_id
ORDER BY product_id;

SELECT count(*) AS total_rows FROM panel_forecasts;

-- ============================================================================
-- SECTION 2: Seasonal GlobalETS (weekly period=7)
-- ============================================================================

.print ''
.print '>>> SECTION 2: GlobalETS with weekly seasonality (seasonal_period=7)'
.print '--------------------------------------------------------------------------'

CREATE OR REPLACE TABLE seasonal_panel AS
    SELECT 'Alpha' AS uid,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           50.0 + 20.0 * SIN(2 * PI() * i / 7.0) + 0.2 * i AS y
    FROM generate_series(0, 55) t(i)
    UNION ALL
    SELECT 'Beta',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           30.0 + 15.0 * COS(2 * PI() * i / 7.0) + 0.3 * i
    FROM generate_series(0, 48) t(i)
    UNION ALL
    SELECT 'Gamma',
           DATE '2024-01-03' + INTERVAL (i) DAY,
           40.0 + 10.0 * SIN(2 * PI() * i / 7.0 + 0.5) + 0.1 * i
    FROM generate_series(0, 41) t(i);

SELECT uid AS series, count(*) AS n_obs FROM seasonal_panel GROUP BY uid ORDER BY uid;

SELECT uid AS series, forecast_step, ROUND(yhat, 2) AS yhat, model_name
FROM ts_forecast_panel_by(
    'seasonal_panel',
    uid,
    ds,
    y,
    'GlobalETS',
    7,
    '1d',
    MAP {'seasonal_period': '7'}
)
ORDER BY series, forecast_step;

-- ============================================================================
-- SECTION 3: Drop rule — series with < 10 valid observations
-- ============================================================================

.print ''
.print '>>> SECTION 3: Drop rule for short series (< 10 valid observations)'
.print '--------------------------------------------------------------------------'

CREATE OR REPLACE TABLE mixed_panel AS
    SELECT 'LongA' AS uid,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           10.0 + i * 0.2 AS y
    FROM generate_series(0, 29) t(i)
    UNION ALL
    SELECT 'LongB',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           20.0 + i * 0.3
    FROM generate_series(0, 24) t(i)
    UNION ALL
    SELECT 'LongC',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           15.0 + i * 0.1
    FROM generate_series(0, 19) t(i)
    UNION ALL
    -- Short series: only 5 rows — will be DROPPED
    SELECT 'ShortX',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           99.0 + i * 1.0
    FROM generate_series(0, 4) t(i);

.print 'Expected: LongA/B/C → GlobalETS model_name, ShortX → DROPPED: too_short'
SELECT uid, model_name, count(*) AS n_rows
FROM ts_forecast_panel_by('mixed_panel', uid, ds, y, 'GlobalETS', 4, '1d')
GROUP BY uid, model_name
ORDER BY uid;

-- ============================================================================
-- [02-2] GlobalTheta + GlobalCroston sections appended here
-- ============================================================================
