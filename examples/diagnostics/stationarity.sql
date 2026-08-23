-- ============================================================================
-- Diagnostics Examples: Stationarity Tests
-- ============================================================================
-- Demonstrates ts_adf / ts_adf_by for ADF unit-root stationarity testing.
-- Plans 01-2 / 01-3 will append KPSS and residual diagnostic sections here.
--
-- Run: ./build/release/duckdb < examples/diagnostics/stationarity.sql
--
-- Requirements:
--   - anofox-forecast extension built (make rust && cmake build)
-- ============================================================================

LOAD anofox_forecast;

.print '============================================================================='
.print 'DIAGNOSTICS EXAMPLES: Stationarity Tests (ts_adf / ts_adf_by)'
.print '============================================================================='

-- ============================================================================
-- SECTION 1: Create synthetic multi-series data
-- ============================================================================
-- Two series with known stationarity properties:
--   - "random_walk"  : I(1) process — non-stationary, ADF should NOT reject H0
--   - "mean_revert"  : AR(1) with φ=0.3 — stationary, ADF should reject H0

.print ''
.print '>>> SECTION 1: Synthetic multi-series data'
.print '-----------------------------------------------------------------------------'

CREATE OR REPLACE TABLE sales_data AS
WITH
rw AS (
    -- Random walk: y_t = y_{t-1} + ε, ε ∈ {-0.5, 0.2} deterministically
    SELECT
        'random_walk' AS product_id,
        (DATE '2023-01-01' + INTERVAL (i-1) DAY) AS ds,
        SUM(CASE WHEN i % 5 = 0 THEN -0.5 ELSE 0.2 END) OVER (ORDER BY i) AS y
    FROM generate_series(1, 50) t(i)
),
mr AS (
    -- Mean-reverting AR(1): y_t = 0.3 * y_{t-1} + noise (bounded, stationary)
    SELECT
        'mean_revert' AS product_id,
        (DATE '2023-01-01' + INTERVAL (i-1) DAY) AS ds,
        3.0 + 0.4 * SIN(i * 0.6) + 0.15 * COS(i * 1.5) AS y
    FROM generate_series(1, 50) t(i)
)
SELECT * FROM rw
UNION ALL
SELECT * FROM mr;

SELECT COUNT(*) AS total_rows, COUNT(DISTINCT product_id) AS n_series
FROM sales_data;

-- ============================================================================
-- SECTION 2: ts_adf — scalar function on a single series
-- ============================================================================

.print ''
.print '>>> SECTION 2: ts_adf scalar function (single series)'
.print '-----------------------------------------------------------------------------'
.print 'ADF test on the mean-reverting series (expected: stationary, p < 0.05)'

SELECT
    (adf).statistic                                        AS t_statistic,
    (adf).p_value                                          AS p_value,
    (adf).lags                                             AS lags_used,
    (adf).is_stationary                                    AS is_stationary,
    ROUND((adf).cv_5pct, 3)                                AS critical_value_5pct
FROM (
    SELECT ts_adf(LIST(y ORDER BY ds)) AS adf
    FROM sales_data
    WHERE product_id = 'mean_revert'
);

.print ''
.print 'ADF test on the random walk (expected: NOT stationary, p > 0.05)'

SELECT
    (adf).statistic  AS t_statistic,
    (adf).p_value    AS p_value,
    (adf).lags       AS lags_used,
    (adf).is_stationary AS is_stationary
FROM (
    SELECT ts_adf(LIST(y ORDER BY ds)) AS adf
    FROM sales_data
    WHERE product_id = 'random_walk'
);

-- ============================================================================
-- SECTION 3: ts_adf_by — grouped macro (one result per series)
-- ============================================================================

.print ''
.print '>>> SECTION 3: ts_adf_by grouped macro (all series)'
.print '-----------------------------------------------------------------------------'
.print 'ADF stationarity test across all groups in one query:'

SELECT
    product_id,
    ROUND((adf).statistic, 4)    AS t_statistic,
    (adf).p_value                AS p_value,
    (adf).lags                   AS lags,
    (adf).is_stationary          AS is_stationary,
    ROUND((adf).cv_1pct, 2)      AS cv_1pct,
    ROUND((adf).cv_5pct, 2)      AS cv_5pct,
    ROUND((adf).cv_10pct, 2)     AS cv_10pct
FROM ts_adf_by('sales_data', product_id, ds, y)
ORDER BY product_id;

-- ============================================================================
-- SECTION 4: ts_adf_by with max_lags override
-- ============================================================================

.print ''
.print '>>> SECTION 4: ts_adf_by with max_lags override'
.print '-----------------------------------------------------------------------------'
.print 'Force max 1 lag (max_lags:=1) vs auto (max_lags:=-1):'

SELECT
    product_id,
    (adf_auto).lags   AS lags_auto,
    (adf_1).lags      AS lags_max1
FROM (
    SELECT product_id,
           ts_adf(LIST(y ORDER BY ds))    AS adf_auto,
           ts_adf(LIST(y ORDER BY ds), 1) AS adf_1
    FROM sales_data
    GROUP BY product_id
)
ORDER BY product_id;

.print ''
.print '============================================================================='
.print 'Done. For KPSS and combined stationarity tests, see plans 01-2/01-3.'
.print '       For residual diagnostics, see examples/diagnostics/residual_diagnostics.sql'
.print '============================================================================='

-- ============================================================================
-- Section 5: KPSS test (STAT-02)
-- KPSS null hypothesis = series IS level-stationary (opposite of ADF).
-- ============================================================================
.print '--- ts_kpss: scalar form ---'
WITH s AS (SELECT i AS ds, sin(i/6.0) + (i%5)*0.01 AS y FROM range(1, 80) t(i))
SELECT (ts_kpss(LIST(y ORDER BY ds))).statistic     AS kpss_stat,
       (ts_kpss(LIST(y ORDER BY ds))).is_stationary AS is_stationary
FROM s;

.print '--- ts_kpss_by: grouped form ---'
SELECT product_id, (kpss).statistic, (kpss).is_stationary
FROM ts_kpss_by('sales_data', product_id, ds, y) ORDER BY product_id;

-- ============================================================================
-- Section 6: Combined ADF + KPSS four-way verdict (STAT-03)
-- ============================================================================
.print '--- ts_stationarity: four-way verdict ---'
SELECT product_id, (stationarity).verdict,
       (stationarity).adf_is_stationary, (stationarity).kpss_is_stationary
FROM ts_stationarity_by('sales_data', product_id, ds, y) ORDER BY product_id;
