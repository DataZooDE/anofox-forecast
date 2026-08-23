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
-- SECTION 4: GlobalTheta — pooled Theta, no seasonal_period required
-- ============================================================================
-- GlobalTheta fits a shared smoothing parameter alpha across all series using
-- the Theta Method (theta=2.0 default). No seasonal period is needed — Theta
-- fits a linear trend + exponential smoothing per series.
-- Best for panels of trended series where minimal configuration is desired.

.print ''
.print '>>> SECTION 4: GlobalTheta (no seasonal_period required)'
.print '--------------------------------------------------------------------------'

CREATE OR REPLACE TABLE panel_sales AS
    SELECT 'Series_A' AS product_id,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           100.0 + i * 0.6 + 5.0 * SIN(2 * PI() * i / 7.0) AS y
    FROM generate_series(0, 29) t(i)
    UNION ALL
    SELECT 'Series_B',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           80.0 + i * 0.4 + 4.0 * COS(2 * PI() * i / 7.0)
    FROM generate_series(0, 27) t(i)
    UNION ALL
    SELECT 'Series_C',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           60.0 + i * 0.8 + 3.0 * SIN(2 * PI() * i / 7.0 + 1.0)
    FROM generate_series(0, 24) t(i);

.print 'GlobalTheta panel forecast (horizon=14, frequency=1d):'
SELECT product_id, forecast_step, ds, ROUND(yhat, 2) AS yhat, model_name
FROM ts_forecast_panel_by(
    'panel_sales',
    product_id,
    ds,
    y,
    'GlobalTheta',
    14,
    '1d'
)
ORDER BY product_id, forecast_step;

.print ''
.print 'Forecast count check (expect 14 rows per series, 42 total):'
SELECT product_id, count(*) AS n_forecasts
FROM ts_forecast_panel_by(
    'panel_sales', product_id, ds, y, 'GlobalTheta', 14, '1d'
)
GROUP BY product_id
ORDER BY product_id;

-- ============================================================================
-- SECTION 5: GlobalCroston — intermittent demand panel (Classic + SBA variants)
-- ============================================================================
-- GlobalCroston fits shared alpha across all intermittent series. The forecast
-- is a FLAT constant per series (demand rate / inter-demand interval).
-- croston_variant: 'Classic' (default) or 'SBA' (Syntetos-Boylan bias correction).
-- Best for spare-parts or irregular-demand panels; output is always non-negative.

.print ''
.print '>>> SECTION 5: GlobalCroston — intermittent demand panel (Classic + SBA)'
.print '--------------------------------------------------------------------------'

-- Intermittent panel: mostly zeros with occasional demand spikes
CREATE OR REPLACE TABLE panel_intermittent AS
    SELECT 'Item_A' AS item_id,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           CASE WHEN i % 4 = 0 THEN 3.0 WHEN i % 7 = 0 THEN 5.0 ELSE 0.0 END AS qty
    FROM generate_series(0, 29) t(i)
    UNION ALL
    SELECT 'Item_B',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           CASE WHEN i % 5 = 0 THEN 2.0 WHEN i % 9 = 0 THEN 4.0 ELSE 0.0 END
    FROM generate_series(0, 27) t(i)
    UNION ALL
    SELECT 'Item_C',
           DATE '2024-01-01' + INTERVAL (i) DAY,
           CASE WHEN i % 3 = 0 THEN 1.0 WHEN i % 11 = 0 THEN 6.0 ELSE 0.0 END
    FROM generate_series(0, 24) t(i);

.print 'Panel demand statistics:'
SELECT item_id,
       count(*) AS n_obs,
       sum(qty) AS total_demand,
       count(CASE WHEN qty > 0 THEN 1 END) AS demand_occurrences
FROM panel_intermittent
GROUP BY item_id
ORDER BY item_id;

.print ''
.print 'GlobalCroston Classic forecast (horizon=6, frequency=1d):'
CREATE OR REPLACE TABLE croston_classic AS
SELECT item_id, forecast_step, ds, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_panel_by(
    'panel_intermittent',
    item_id,
    ds,
    qty,
    'GlobalCroston',
    6,
    '1d'
)
ORDER BY item_id, forecast_step;

SELECT * FROM croston_classic;

.print ''
.print 'GlobalCroston SBA forecast (croston_variant := SBA):'
CREATE OR REPLACE TABLE croston_sba AS
SELECT item_id, forecast_step, ds, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_panel_by(
    'panel_intermittent',
    item_id,
    ds,
    qty,
    'GlobalCroston',
    6,
    '1d',
    MAP {'croston_variant': 'SBA'}
)
ORDER BY item_id, forecast_step;

SELECT * FROM croston_sba;

.print ''
.print 'Croston flatness check (each series should have identical yhat across steps):'
SELECT item_id,
       model_name,
       min(ROUND(yhat, 6)) AS min_yhat,
       max(ROUND(yhat, 6)) AS max_yhat,
       CASE WHEN min(yhat) = max(yhat) THEN 'FLAT' ELSE 'NOT_FLAT' END AS flat_check
FROM croston_classic
GROUP BY item_id, model_name
ORDER BY item_id;

.print ''
.print 'SBA <= Classic check (SBA applies downward bias correction):'
SELECT c.item_id,
       ROUND(c.yhat, 4) AS classic_yhat,
       ROUND(s.yhat, 4) AS sba_yhat,
       CASE WHEN s.yhat <= c.yhat + 0.0001 THEN 'SBA_LE_CLASSIC' ELSE 'FAIL' END AS check
FROM croston_classic c
JOIN croston_sba s ON c.item_id = s.item_id AND c.forecast_step = s.forecast_step
ORDER BY c.item_id, c.forecast_step
LIMIT 9;

-- ============================================================================
-- SECTION 6: Method comparison — GlobalETS vs GlobalTheta on the same panel
-- ============================================================================
-- Shows that ts_forecast_panel_by is method-swappable: same source table,
-- same columns, different method string.

.print ''
.print '>>> SECTION 6: Method comparison — GlobalETS vs GlobalTheta'
.print '--------------------------------------------------------------------------'

SELECT 'GlobalETS' AS method, product_id, forecast_step, ROUND(yhat, 2) AS yhat
FROM ts_forecast_panel_by('panel_sales', product_id, ds, y, 'GlobalETS', 7, '1d')
UNION ALL
SELECT 'GlobalTheta', product_id, forecast_step, ROUND(yhat, 2)
FROM ts_forecast_panel_by('panel_sales', product_id, ds, y, 'GlobalTheta', 7, '1d')
ORDER BY product_id, method, forecast_step;

.print ''
.print 'All sections complete — GLOB-02 (GlobalTheta) and GLOB-03 (GlobalCroston) verified.'
