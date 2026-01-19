-- =============================================================================
-- Decomposition Examples - Synthetic Data
-- =============================================================================
-- This script demonstrates time series decomposition with the anofox-forecast
-- extension using 5 patterns from basic to advanced.
--
-- Run: ./build/release/duckdb < examples/decomposition/synthetic_decomposition_examples.sql
-- =============================================================================

-- Load extension
LOAD anofox_forecast;

.print '============================================================================='
.print 'DECOMPOSITION EXAMPLES - Synthetic Data'
.print '============================================================================='

-- =============================================================================
-- SECTION 1: Detrending (ts_detrend)
-- =============================================================================
-- Use case: Remove trend component to reveal seasonal patterns.

.print ''
.print '>>> SECTION 1: Detrending (ts_detrend)'
.print '-----------------------------------------------------------------------------'

-- Create data with strong trend and seasonality
CREATE OR REPLACE TABLE trend_data AS
SELECT
    i,
    -- Base value with strong upward trend + seasonal pattern + noise
    50.0 + i * 2.0 + 20.0 * SIN(2 * PI() * i / 12.0) + (RANDOM() - 0.5) * 10 AS value
FROM generate_series(0, 47) AS t(i);

.print 'Data with trend and seasonality (first 12 values):'
SELECT i, ROUND(value, 2) AS value FROM trend_data WHERE i < 12 ORDER BY i;

.print ''
.print 'Detrended result (auto method):'
SELECT
    (ts_detrend(LIST(value ORDER BY i))).method AS detected_method,
    (ts_detrend(LIST(value ORDER BY i))).n_params AS n_params,
    ROUND((ts_detrend(LIST(value ORDER BY i))).rss, 2) AS residual_sum_sq,
    length((ts_detrend(LIST(value ORDER BY i))).trend) AS n_points
FROM trend_data;

-- =============================================================================
-- SECTION 2: Seasonal Decomposition (ts_decompose_seasonal)
-- =============================================================================
-- Use case: Separate trend, seasonal, and remainder components.

.print ''
.print '>>> SECTION 2: Seasonal Decomposition (ts_decompose_seasonal)'
.print '-----------------------------------------------------------------------------'

-- Create clear seasonal data
CREATE OR REPLACE TABLE seasonal_data AS
SELECT
    i,
    -- Seasonal pattern with period=4 (quarterly)
    100.0 + 5.0 * i / 24.0 + 20.0 * SIN(2 * PI() * i / 4.0) + (RANDOM() - 0.5) * 5 AS value
FROM generate_series(0, 23) AS t(i);

.print 'Data with quarterly seasonality:'
SELECT i, ROUND(value, 2) AS value FROM seasonal_data ORDER BY i;

.print ''
.print 'Additive decomposition (period=4):'
SELECT
    (ts_decompose_seasonal(LIST(value ORDER BY i), 4, 'additive')).method AS method,
    (ts_decompose_seasonal(LIST(value ORDER BY i), 4, 'additive')).period AS period,
    length((ts_decompose_seasonal(LIST(value ORDER BY i), 4, 'additive')).trend) AS n_points
FROM seasonal_data;

-- =============================================================================
-- SECTION 3: MSTL Decomposition (Multi-Seasonal)
-- =============================================================================
-- Use case: Handle multiple seasonal periods simultaneously.

.print ''
.print '>>> SECTION 3: MSTL Decomposition (Multi-Seasonal)'
.print '-----------------------------------------------------------------------------'

-- Create data with multiple seasonal patterns
CREATE OR REPLACE TABLE multi_seasonal AS
SELECT
    i,
    -- Weekly (7) + Monthly (30) patterns
    100.0
    + 20.0 * SIN(2 * PI() * i / 7.0)   -- weekly
    + 10.0 * SIN(2 * PI() * i / 30.0)  -- monthly
    + (RANDOM() - 0.5) * 5 AS value
FROM generate_series(0, 89) AS t(i);

.print 'Data with multiple seasonal patterns (weekly + monthly):'
SELECT
    COUNT(*) AS n_points,
    ROUND(AVG(value), 2) AS mean_value,
    ROUND(STDDEV(value), 2) AS std_value
FROM multi_seasonal;

.print ''
.print 'MSTL decomposition result:'
SELECT
    length((_ts_mstl_decomposition_by(LIST(value ORDER BY i))).trend) AS trend_length,
    length((_ts_mstl_decomposition_by(LIST(value ORDER BY i))).remainder) AS remainder_length,
    (_ts_mstl_decomposition_by(LIST(value ORDER BY i))).periods AS detected_periods
FROM multi_seasonal;

-- =============================================================================
-- SECTION 4: Decomposition for Grouped Series
-- =============================================================================
-- Use case: Apply decomposition to multiple time series.

.print ''
.print '>>> SECTION 4: Decomposition for Grouped Series'
.print '-----------------------------------------------------------------------------'

-- Create multi-series data
CREATE OR REPLACE TABLE multi_series_decomp AS
SELECT * FROM (
    -- Series A: Strong seasonality
    SELECT
        'A' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        100.0 + 30.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 5 AS value
    FROM generate_series(0, 27) AS t(i)
    UNION ALL
    -- Series B: Trend dominant
    SELECT
        'B' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        50.0 + i * 2.0 + (RANDOM() - 0.5) * 10 AS value
    FROM generate_series(0, 27) AS t(i)
);

.print 'Multi-series data summary:'
SELECT series_id, COUNT(*) AS n_points, ROUND(AVG(value), 2) AS avg_value
FROM multi_series_decomp GROUP BY series_id ORDER BY series_id;

.print ''
.print 'MSTL decomposition by series (using table macro):'
SELECT
    id AS series_id,
    length((decomposition).trend) AS trend_length,
    (decomposition).periods AS detected_periods
FROM ts_mstl_decomposition_by('multi_series_decomp', series_id, date, value, MAP{})
ORDER BY series_id;

-- =============================================================================
-- SECTION 5: Extract Components
-- =============================================================================
-- Use case: Access individual decomposition components.

.print ''
.print '>>> SECTION 5: Extract Components'
.print '-----------------------------------------------------------------------------'

-- Create simple seasonal data
CREATE OR REPLACE TABLE extract_demo AS
SELECT
    i,
    100.0 + 10.0 * SIN(2 * PI() * i / 6.0) + (RANDOM() - 0.5) * 3 AS value
FROM generate_series(0, 23) AS t(i);

.print 'Extract trend, seasonal, remainder components:'
WITH decomposed AS (
    SELECT ts_decompose_seasonal(LIST(value ORDER BY i), 6, 'additive') AS result
    FROM extract_demo
)
SELECT
    (result).method AS method,
    (result).period AS period,
    ROUND((result).trend[1], 2) AS first_trend,
    ROUND((result).seasonal[1], 2) AS first_seasonal,
    ROUND((result).remainder[1], 2) AS first_remainder
FROM decomposed;

.print ''
.print 'MSTL components (trend first 5 values):'
WITH mstl_result AS (
    SELECT _ts_mstl_decomposition_by(LIST(value ORDER BY i)) AS result
    FROM extract_demo
)
SELECT
    (result).trend[1:5] AS first_5_trend,
    (result).remainder[1:5] AS first_5_remainder
FROM mstl_result;

-- =============================================================================
-- CLEANUP
-- =============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS trend_data;
DROP TABLE IF EXISTS seasonal_data;
DROP TABLE IF EXISTS multi_seasonal;
DROP TABLE IF EXISTS multi_series_decomp;
DROP TABLE IF EXISTS extract_demo;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'DECOMPOSITION EXAMPLES COMPLETE'
.print '============================================================================='
