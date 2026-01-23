-- ============================================================================
-- Decomposition Examples - Synthetic Data
-- ============================================================================
-- This script demonstrates time series decomposition with the anofox-forecast
-- extension using the ts_mstl_decomposition_by and ts_detrend_by table macros.
--
-- RECOMMENDED APPROACH:
--   ts_mstl_decomposition_by - Full seasonal decomposition (trend + seasonal + remainder)
--   ts_detrend_by            - Simple trend removal (when you only need detrended data)
--
-- MAP{} BEHAVIOR:
--   When you pass MAP{} (empty params) to ts_mstl_decomposition_by:
--   - Auto-detect seasonal periods from the data
--   - Works well for most time series with clear patterns
--   - For explicit control, use: MAP{'periods': '[7, 30]'} for weekly + monthly
--
-- Run: ./build/release/duckdb < examples/decomposition/synthetic_decomposition_examples.sql
-- ============================================================================

-- Load extension
LOAD anofox_forecast;
INSTALL json;
LOAD json;

.print '============================================================================='
.print 'DECOMPOSITION EXAMPLES - Using ts_mstl_decomposition_by'
.print '============================================================================='

-- ============================================================================
-- SECTION 1: Basic MSTL Decomposition for Multiple Series
-- ============================================================================
-- Use ts_mstl_decomposition_by to decompose grouped time series into
-- trend, seasonal, and remainder components.

.print ''
.print '>>> SECTION 1: Basic MSTL Decomposition'
.print '-----------------------------------------------------------------------------'

-- Create multi-series data with different seasonal patterns
CREATE OR REPLACE TABLE multi_series AS
SELECT * FROM (
    -- Series A: Strong weekly seasonality
    SELECT
        'Series_A' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        100.0 + i * 0.5 + 30.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 10 AS value
    FROM generate_series(0, 89) AS t(i)
    UNION ALL
    -- Series B: Monthly seasonality with trend
    SELECT
        'Series_B' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        200.0 + i * 1.0 + 50.0 * SIN(2 * PI() * i / 30.0) + (RANDOM() - 0.5) * 15 AS value
    FROM generate_series(0, 89) AS t(i)
    UNION ALL
    -- Series C: Mixed weekly + monthly patterns
    SELECT
        'Series_C' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        150.0
        + 20.0 * SIN(2 * PI() * i / 7.0)
        + 40.0 * SIN(2 * PI() * i / 30.0)
        + (RANDOM() - 0.5) * 8 AS value
    FROM generate_series(0, 89) AS t(i)
);

.print 'Multi-series data summary:'
SELECT series_id, COUNT(*) AS n_points, ROUND(AVG(value), 2) AS avg_value
FROM multi_series GROUP BY series_id ORDER BY series_id;

-- 1.1: Basic decomposition (auto-detect periods)
.print ''
.print 'Section 1.1: Basic MSTL Decomposition (auto-detect periods)'

SELECT
    id,
    length(trend) AS trend_length,
    length(remainder) AS remainder_length,
    periods AS detected_periods
FROM ts_mstl_decomposition_by('multi_series', series_id, date, value, MAP{});

-- ============================================================================
-- SECTION 2: Decomposition with Explicit Periods
-- ============================================================================

.print ''
.print '>>> SECTION 2: Decomposition with Explicit Periods'
.print '-----------------------------------------------------------------------------'

-- 2.1: Specify weekly period
.print 'Section 2.1: Weekly decomposition (period=7)'

SELECT
    id,
    length(trend) AS n_points,
    periods
FROM ts_mstl_decomposition_by('multi_series', series_id, date, value,
    MAP{'periods': '[7]'});

-- 2.2: Multiple seasonal periods
.print ''
.print 'Section 2.2: Multiple periods (weekly + monthly)'

SELECT
    id,
    length(trend) AS n_points,
    periods
FROM ts_mstl_decomposition_by('multi_series', series_id, date, value,
    MAP{'periods': '[7, 30]'});

-- ============================================================================
-- SECTION 3: Extracting Components
-- ============================================================================

.print ''
.print '>>> SECTION 3: Extracting Components'
.print '-----------------------------------------------------------------------------'

-- 3.1: Extract trend for analysis
.print 'Section 3.1: First 5 trend values per series'

SELECT
    id,
    trend[1:5] AS first_5_trend,
    remainder[1:5] AS first_5_remainder
FROM ts_mstl_decomposition_by('multi_series', series_id, date, value, MAP{});

-- 3.2: Compute trend statistics
.print ''
.print 'Section 3.2: Trend statistics per series'

WITH decomposed AS (
    SELECT * FROM ts_mstl_decomposition_by('multi_series', series_id, date, value, MAP{})
)
SELECT
    id,
    ROUND(list_avg(trend), 2) AS mean_trend,
    ROUND(trend[length(trend)] - trend[1], 2) AS trend_change,
    ROUND(list_stddev_pop(remainder), 2) AS remainder_std
FROM decomposed;

-- ============================================================================
-- SECTION 4: Real-World Scenarios
-- ============================================================================

.print ''
.print '>>> SECTION 4: Real-World Scenarios'
.print '-----------------------------------------------------------------------------'

-- 4.1: Retail sales with multiple seasonalities
.print 'Section 4.1: Retail Sales Decomposition'

CREATE OR REPLACE TABLE retail_sales AS
SELECT * FROM (
    -- Store 1: Strong weekly pattern
    SELECT
        'Store_1' AS store_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        ROUND(
            5000.0
            + i * 10  -- growth trend
            + 1500.0 * SIN(2 * PI() * i / 7.0)  -- weekly
            + 500.0 * SIN(2 * PI() * i / 30.0)  -- monthly
            + (RANDOM() - 0.5) * 300
        , 0)::INT AS sales
    FROM generate_series(0, 179) AS t(i)
    UNION ALL
    -- Store 2: Declining trend, weak seasonality
    SELECT
        'Store_2' AS store_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        ROUND(
            8000.0
            - i * 5  -- declining trend
            + 800.0 * SIN(2 * PI() * i / 7.0)
            + (RANDOM() - 0.5) * 400
        , 0)::INT AS sales
    FROM generate_series(0, 179) AS t(i)
    UNION ALL
    -- Store 3: Stable with strong seasonality
    SELECT
        'Store_3' AS store_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        ROUND(
            6000.0
            + 2000.0 * SIN(2 * PI() * i / 7.0)
            + 1000.0 * SIN(2 * PI() * i / 30.0)
            + (RANDOM() - 0.5) * 250
        , 0)::INT AS sales
    FROM generate_series(0, 179) AS t(i)
);

SELECT
    id AS store,
    length(trend) AS n_days,
    periods AS detected_periods,
    ROUND(trend[length(trend)] - trend[1], 0) AS trend_change_6months
FROM ts_mstl_decomposition_by('retail_sales', store_id, date, sales, MAP{});

-- 4.2: Sensor data decomposition
.print ''
.print 'Section 4.2: Sensor Data Decomposition'

CREATE OR REPLACE TABLE sensor_readings AS
SELECT * FROM (
    -- Sensor A: Daily cycle (temperature)
    SELECT
        'Temp_Sensor' AS sensor_id,
        '2024-01-01 00:00:00'::TIMESTAMP + INTERVAL (i) HOUR AS timestamp,
        ROUND(
            20.0
            + 5.0 * SIN(2 * PI() * i / 24.0)  -- daily cycle
            + i * 0.01  -- slight warming trend
            + (RANDOM() - 0.5) * 2
        , 2) AS reading
    FROM generate_series(0, 167) AS t(i)
    UNION ALL
    -- Sensor B: Weekly cycle (traffic)
    SELECT
        'Traffic_Sensor' AS sensor_id,
        '2024-01-01 00:00:00'::TIMESTAMP + INTERVAL (i) HOUR AS timestamp,
        ROUND(
            100.0
            + 50.0 * SIN(2 * PI() * i / 168.0)  -- weekly cycle (168 hours)
            + 30.0 * SIN(2 * PI() * i / 24.0)   -- daily cycle
            + (RANDOM() - 0.5) * 20
        , 2) AS reading
    FROM generate_series(0, 167) AS t(i)
);

SELECT
    id AS sensor,
    length(trend) AS n_hours,
    periods AS detected_periods
FROM ts_mstl_decomposition_by('sensor_readings', sensor_id, timestamp, reading, MAP{});

-- ============================================================================
-- SECTION 5: Detrending with ts_detrend_by
-- ============================================================================
-- Use ts_detrend_by to remove trend from grouped time series.
-- Methods: 'linear', 'quadratic', 'cubic', 'auto' (default)

.print ''
.print '>>> SECTION 5: Detrending with ts_detrend_by'
.print '-----------------------------------------------------------------------------'

-- 5.1: Linear detrending
.print 'Section 5.1: Linear Detrending'

SELECT
    id,
    method,
    n_params,
    ROUND(rss, 2) AS rss,
    trend[1:5] AS first_5_trend
FROM ts_detrend_by('multi_series', series_id, date, value, 'linear');

-- 5.2: Quadratic detrending
.print ''
.print 'Section 5.2: Quadratic Detrending'

SELECT
    id,
    method,
    n_params,
    ROUND(rss, 2) AS rss
FROM ts_detrend_by('multi_series', series_id, date, value, 'quadratic');

-- 5.3: Auto detrending (automatically selects best method)
.print ''
.print 'Section 5.3: Auto Detrending'

SELECT
    id,
    method,
    n_params,
    ROUND(rss, 2) AS rss
FROM ts_detrend_by('multi_series', series_id, date, value, 'auto');

-- ============================================================================
-- SECTION 6: Comparing Decomposition Results
-- ============================================================================

.print ''
.print '>>> SECTION 6: Comparing Decomposition Results'
.print '-----------------------------------------------------------------------------'

-- 6.1: Summary comparison
.print 'Section 6.1: Decomposition Summary Comparison'

WITH decomposed AS (
    SELECT * FROM ts_mstl_decomposition_by('retail_sales', store_id, date, sales, MAP{})
)
SELECT
    id AS store,
    ROUND(list_avg(trend), 0) AS avg_trend,
    ROUND(list_stddev_pop(trend), 0) AS trend_volatility,
    ROUND(list_stddev_pop(remainder), 0) AS noise_level,
    CASE
        WHEN list_stddev_pop(remainder) < 300 THEN 'Low noise'
        WHEN list_stddev_pop(remainder) < 500 THEN 'Moderate noise'
        ELSE 'High noise'
    END AS noise_assessment
FROM decomposed
ORDER BY noise_level;

-- ============================================================================
-- CLEANUP
-- ============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS multi_series;
DROP TABLE IF EXISTS retail_sales;
DROP TABLE IF EXISTS sensor_readings;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'DECOMPOSITION EXAMPLES COMPLETE'
.print '============================================================================='
