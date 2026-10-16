-- ============================================================================
-- Diagnostics Examples: Stationarity Tests
-- ============================================================================
-- Demonstrates ts_adf / ts_adf_by (statsmodels-exact, AIC lag selection),
-- ts_kpss / ts_kpss_by (tseries-exact, including the 'ct' trend variant), and
-- ts_stationarity / ts_stationarity_by (combined D-04 four-way verdict) --
-- all with the regression := 'c' | 'ct' parameter (Phase 17, D-05/D-05a/D-04).
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
.print 'Done with ts_adf / ts_adf_by. Continuing with KPSS, combined verdict,'
.print 'and the regression := ''ct'' trend-stationary case below.'
.print '       For residual diagnostics, see examples/diagnostics/residuals.sql'
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

-- ============================================================================
-- Section 7: regression := 'ct' (constant + linear trend), D-05/D-05a/D-04
-- ============================================================================
-- A deterministic-trend series: under regression := 'c' (default) ADF fails
-- to find a unit root around a flat mean (the series obviously isn't flat),
-- so the combined verdict reads non_stationary; under regression := 'ct'
-- both tests account for the trend, and the verdict becomes trend_stationary.

.print ''
.print '>>> SECTION 7: regression := ''ct'' trend-stationary case'
.print '-----------------------------------------------------------------------------'

CREATE OR REPLACE TABLE trend_stationary_data AS
SELECT 'ts' AS id, DATE '2024-01-01' + INTERVAL (i) DAY AS ds, v
FROM (SELECT unnest(range(100)) AS i, unnest([
    11.6868,11.6724,11.7833,11.1672,11.9244,11.9678,11.7347,11.9132,13.0209,
    13.9755,12.6389,11.3045,11.1077,12.8573,12.3706,12.1477,13.7922,14.5765,
    13.4204,10.7194,11.9456,13.974,14.2568,12.7642,12.7654,14.6512,15.689,
    12.7434,11.9204,13.6517,15.4728,16.7095,15.8291,15.5605,16.3353,17.4651,
    17.2086,18.1492,19.4742,19.7893,19.7197,20.1087,18.5176,19.301,17.9914,
    19.8214,19.0297,17.6203,17.5362,19.0964,19.0789,20.4123,21.7995,20.9125,
    21.1322,22.2103,21.8015,24.1885,23.4572,22.7352,22.3906,22.7709,23.1019,
    24.8245,23.6303,23.6606,24.6127,23.1483,23.5438,24.2914,22.9941,23.6819,
    25.4222,27.0908,27.1649,26.4485,25.8,27.1473,27.7016,27.4023,26.4704,
    26.2581,26.9843,26.2706,28.1165,28.9246,28.1264,27.9331,27.9118,30.5434,
    29.5322,28.9346,27.1721,27.8209,27.5195,28.6054,28.6876,29.5953,31.4823,
    29.888
]::DOUBLE[]) AS v);

.print 'ADF/KPSS statistics under ''ct'' vs the default ''c'':'
SELECT
    (ts_adf(LIST(v ORDER BY ds), -1, 'c')).statistic   AS adf_c_stat,
    (ts_adf(LIST(v ORDER BY ds), -1, 'ct')).statistic  AS adf_ct_stat,
    (ts_kpss(LIST(v ORDER BY ds), -1, 'c')).statistic  AS kpss_c_stat,
    (ts_kpss(LIST(v ORDER BY ds), -1, 'ct')).statistic AS kpss_ct_stat
FROM trend_stationary_data;

.print 'Combined verdict, regression := ''c'' (default) vs ''ct'':'
SELECT
    (ts_stationarity(LIST(v ORDER BY ds), 'c')).verdict  AS verdict_c,
    (ts_stationarity(LIST(v ORDER BY ds), 'ct')).verdict AS verdict_ct
FROM trend_stationary_data;

.print 'Same, via ts_stationarity_by:'
SELECT id, (stationarity).verdict
FROM ts_stationarity_by('trend_stationary_data', id, ds, v, regression := 'ct');
