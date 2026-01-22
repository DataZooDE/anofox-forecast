-- ============================================================================
-- Synthetic Peak Detection Examples
-- ============================================================================
-- This file demonstrates peak detection and timing analysis using synthetic
-- (generated) data. Use this to learn the API before applying to your datasets.
--
-- Functions used:
--   - ts_detect_peaks_by: Detect local maxima for multiple series
--   - ts_analyze_peak_timing_by: Analyze timing consistency for multiple series
--
-- Prerequisites:
--   - anofox_forecast extension loaded
-- ============================================================================

-- Load the extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
INSTALL json;
LOAD json;

-- ============================================================================
-- SECTION 1: Peak Detection for Multiple Series
-- ============================================================================
-- Use ts_detect_peaks_by to detect local maxima across grouped time series.

SELECT '=== Section 1: Peak Detection for Multiple Series ===' AS section;

-- Generate multi-series data: 3 product lines with different peak patterns
CREATE OR REPLACE TABLE product_sales AS
SELECT * FROM (
    -- Product A: Strong weekly peaks (high traffic product)
    SELECT
        'Product_A' AS product_id,
        '2024-01-01'::DATE + (i * INTERVAL '1 day') AS date,
        ROUND(
            1000.0
            + 500.0 * SIN(2 * PI() * i / 7.0)  -- weekly cycle
            + i * 2.0  -- trend
            + (i % 13 - 6) * 20  -- noise
        , 0)::INT AS sales
    FROM generate_series(0, 55) AS t(i)
    UNION ALL
    -- Product B: Monthly peaks (seasonal product)
    SELECT
        'Product_B' AS product_id,
        '2024-01-01'::DATE + (i * INTERVAL '1 day') AS date,
        ROUND(
            800.0
            + 400.0 * SIN(2 * PI() * i / 30.0)  -- monthly cycle
            + (i % 11 - 5) * 15
        , 0)::INT AS sales
    FROM generate_series(0, 55) AS t(i)
    UNION ALL
    -- Product C: Irregular peaks (promotional product)
    SELECT
        'Product_C' AS product_id,
        '2024-01-01'::DATE + (i * INTERVAL '1 day') AS date,
        ROUND(
            600.0
            + CASE WHEN i IN (7, 21, 35, 49) THEN 800.0 ELSE 0.0 END  -- promo spikes
            + (i % 7 - 3) * 30
        , 0)::INT AS sales
    FROM generate_series(0, 55) AS t(i)
);

-- 1.1: Basic peak detection (default parameters)
SELECT 'Section 1.1: Basic Peak Detection' AS step;

SELECT
    id,
    n_peaks,
    mean_period,
    peaks[1:3] AS first_3_peaks
FROM ts_detect_peaks_by('product_sales', product_id, date, sales, MAP{});

-- 1.2: Peak detection with prominence filter
SELECT 'Section 1.2: High Prominence Peaks Only' AS step;

SELECT
    id,
    n_peaks,
    mean_period,
    peaks
FROM ts_detect_peaks_by('product_sales', product_id, date, sales,
    MAP{'min_prominence': '0.3'});

-- 1.3: Peak detection with minimum distance
SELECT 'Section 1.3: Peaks with Minimum Distance' AS step;

SELECT
    id,
    n_peaks,
    mean_period,
    inter_peak_distances
FROM ts_detect_peaks_by('product_sales', product_id, date, sales,
    MAP{'min_distance': '5'});

-- 1.4: Combined prominence and distance filters
SELECT 'Section 1.4: Combined Filters (prominence + distance)' AS step;

SELECT
    id,
    n_peaks,
    mean_period,
    peaks
FROM ts_detect_peaks_by('product_sales', product_id, date, sales,
    MAP{'min_prominence': '0.2', 'min_distance': '7'});

-- ============================================================================
-- SECTION 2: Peak Timing Analysis for Multiple Series
-- ============================================================================
-- Use ts_analyze_peak_timing_by to understand when peaks occur within cycles.

SELECT '=== Section 2: Peak Timing Analysis ===' AS section;

-- 2.1: Analyze weekly peak timing
SELECT 'Section 2.1: Weekly Peak Timing Consistency' AS step;

SELECT
    id,
    n_peaks,
    ROUND(variability_score, 3) AS variability,
    is_stable AS consistent_timing
FROM ts_analyze_peak_timing_by('product_sales', product_id, date, sales, 7.0, MAP{});

-- 2.2: Interpret timing results
SELECT 'Section 2.2: Peak Timing Interpretation' AS step;

SELECT
    id,
    n_peaks,
    ROUND(variability_score, 3) AS variability,
    is_stable,
    CASE
        WHEN variability_score < 0.1 THEN 'Very Consistent - peaks highly predictable'
        WHEN variability_score < 0.3 THEN 'Consistent - peaks moderately predictable'
        WHEN variability_score < 0.5 THEN 'Variable - some timing variation'
        ELSE 'Highly Variable - unpredictable peaks'
    END AS interpretation
FROM ts_analyze_peak_timing_by('product_sales', product_id, date, sales, 7.0, MAP{});

-- ============================================================================
-- SECTION 3: Real-World Scenarios
-- ============================================================================

SELECT '=== Section 3: Real-World Scenarios ===' AS section;

-- 3.1: Sensor anomaly detection across multiple devices
SELECT 'Section 3.1: Sensor Anomaly Detection' AS step;

CREATE OR REPLACE TABLE sensor_data AS
SELECT * FROM (
    -- Sensor 1: Normal with occasional spikes
    SELECT
        'Sensor_1' AS sensor_id,
        '2024-01-01 00:00:00'::TIMESTAMP + (i * INTERVAL '1 minute') AS timestamp,
        ROUND(
            50.0
            + 5.0 * SIN(i * 0.1)
            + CASE WHEN i IN (50, 150, 250) THEN 30.0 ELSE 0.0 END
        , 2) AS temperature
    FROM generate_series(0, 299) AS t(i)
    UNION ALL
    -- Sensor 2: More frequent anomalies
    SELECT
        'Sensor_2' AS sensor_id,
        '2024-01-01 00:00:00'::TIMESTAMP + (i * INTERVAL '1 minute') AS timestamp,
        ROUND(
            55.0
            + 3.0 * SIN(i * 0.15)
            + CASE WHEN i % 60 BETWEEN 28 AND 32 THEN 25.0 ELSE 0.0 END
        , 2) AS temperature
    FROM generate_series(0, 299) AS t(i)
    UNION ALL
    -- Sensor 3: Stable, few peaks
    SELECT
        'Sensor_3' AS sensor_id,
        '2024-01-01 00:00:00'::TIMESTAMP + (i * INTERVAL '1 minute') AS timestamp,
        ROUND(
            48.0
            + 2.0 * SIN(i * 0.08)
            + (i % 11 - 5) * 0.3
        , 2) AS temperature
    FROM generate_series(0, 299) AS t(i)
);

SELECT
    id,
    n_peaks AS anomaly_count,
    mean_period AS avg_time_between_anomalies,
    CASE
        WHEN n_peaks > 5 THEN 'HIGH - Investigate sensor'
        WHEN n_peaks > 2 THEN 'MEDIUM - Monitor closely'
        ELSE 'LOW - Normal operation'
    END AS alert_level
FROM ts_detect_peaks_by('sensor_data', sensor_id, timestamp, temperature,
    MAP{'min_prominence': '0.4'});

-- 3.2: Website traffic peak analysis by region
SELECT 'Section 3.2: Regional Traffic Peaks' AS step;

CREATE OR REPLACE TABLE regional_traffic AS
SELECT * FROM (
    -- US traffic: peaks at 10am and 3pm EST
    SELECT
        'US_East' AS region,
        '2024-01-01 00:00:00'::TIMESTAMP + (i * INTERVAL '1 hour') AS timestamp,
        ROUND(
            1000.0
            + 800.0 * EXP(-0.5 * POWER((i % 24 - 10) / 2.0, 2))
            + 600.0 * EXP(-0.5 * POWER((i % 24 - 15) / 2.0, 2))
            + (i % 17 - 8) * 30
        , 0)::INT AS visitors
    FROM generate_series(0, 167) AS t(i)
    UNION ALL
    -- EU traffic: peaks at different hours
    SELECT
        'EU_West' AS region,
        '2024-01-01 00:00:00'::TIMESTAMP + (i * INTERVAL '1 hour') AS timestamp,
        ROUND(
            800.0
            + 700.0 * EXP(-0.5 * POWER((i % 24 - 9) / 2.0, 2))
            + 500.0 * EXP(-0.5 * POWER((i % 24 - 14) / 2.0, 2))
            + (i % 13 - 6) * 25
        , 0)::INT AS visitors
    FROM generate_series(0, 167) AS t(i)
    UNION ALL
    -- Asia traffic: single peak pattern
    SELECT
        'Asia_East' AS region,
        '2024-01-01 00:00:00'::TIMESTAMP + (i * INTERVAL '1 hour') AS timestamp,
        ROUND(
            600.0
            + 900.0 * EXP(-0.5 * POWER((i % 24 - 11) / 3.0, 2))
            + (i % 19 - 9) * 20
        , 0)::INT AS visitors
    FROM generate_series(0, 167) AS t(i)
);

-- Detect daily peaks per region
SELECT
    id AS region,
    n_peaks AS daily_peaks_7days,
    ROUND(mean_period, 1) AS avg_hours_between_peaks
FROM ts_detect_peaks_by('regional_traffic', region, timestamp, visitors,
    MAP{'min_prominence': '0.3'});

-- Analyze timing consistency
SELECT
    id AS region,
    n_peaks,
    ROUND(variability_score, 3) AS timing_variability,
    is_stable AS predictable_peaks
FROM ts_analyze_peak_timing_by('regional_traffic', region, timestamp, visitors, 24.0, MAP{});

-- ============================================================================
-- SECTION 4: Comparing Peak Patterns
-- ============================================================================

SELECT '=== Section 4: Comparing Peak Patterns ===' AS section;

-- 4.1: Summary comparison across all products
SELECT 'Section 4.1: Product Peak Pattern Summary' AS step;

WITH peaks AS (
    SELECT * FROM ts_detect_peaks_by('product_sales', product_id, date, sales, MAP{})
),
timing AS (
    SELECT * FROM ts_analyze_peak_timing_by('product_sales', product_id, date, sales, 7.0, MAP{})
)
SELECT
    p.id AS product,
    p.n_peaks,
    ROUND(p.mean_period, 1) AS avg_days_between_peaks,
    ROUND(t.variability_score, 3) AS timing_variability,
    t.is_stable AS predictable,
    CASE
        WHEN p.n_peaks > 6 AND t.is_stable THEN 'High frequency, predictable'
        WHEN p.n_peaks > 6 AND NOT t.is_stable THEN 'High frequency, variable'
        WHEN p.n_peaks <= 6 AND t.is_stable THEN 'Low frequency, predictable'
        ELSE 'Low frequency, variable'
    END AS pattern_type
FROM peaks p
JOIN timing t ON p.id = t.id
ORDER BY p.n_peaks DESC;

-- ============================================================================
-- CLEANUP
-- ============================================================================

SELECT '=== Examples Complete ===' AS section;

-- Optionally drop temporary tables
-- DROP TABLE IF EXISTS product_sales;
-- DROP TABLE IF EXISTS sensor_data;
-- DROP TABLE IF EXISTS regional_traffic;
