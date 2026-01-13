-- ============================================================================
-- Synthetic Seasonality Classification Examples
-- ============================================================================
-- This file demonstrates seasonality classification using synthetic
-- (generated) data. Use this to learn the API before applying to your datasets.
--
-- Patterns included:
--   1. Basic Classification - Classify seasonality type with ts_classify_seasonality
--   2. Aggregate Classification - Group-level analysis with ts_classify_seasonality_agg
--   3. Table Macro - Batch processing with ts_classify_seasonality_by
--
-- Prerequisites:
--   - anofox_forecast extension loaded
-- ============================================================================

-- Load the extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- ============================================================================
-- PATTERN 1: Basic Seasonality Classification
-- ============================================================================
-- Scenario: Classify the type of seasonal pattern in time series
-- Use cases: Forecasting method selection, demand characterization, pattern recognition

SELECT
    '=== Pattern 1: Basic Seasonality Classification ===' AS section;

-- Section 1.1: Generate stable weekly seasonal data
SELECT 'Section 1.1: Stable Weekly Seasonal Pattern' AS step;

-- Create weekly seasonal data: 12 weeks of daily data
CREATE OR REPLACE TABLE weekly_seasonal AS
SELECT
    '2024-01-01'::DATE + (i * INTERVAL '1 day') AS date,
    i + 1 AS day_index,
    EXTRACT(DOW FROM ('2024-01-01'::DATE + (i * INTERVAL '1 day')))::INT AS day_of_week,
    ROUND(
        1000.0  -- base value
        + 400.0 * SIN(2 * PI() * (i % 7) / 7.0)  -- weekly pattern
        + 200.0 * COS(2 * PI() * (i % 7) / 7.0)  -- secondary weekly component
        + (RANDOM() - 0.5) * 50  -- small noise
    , 2) AS value
FROM generate_series(0, 83) AS t(i);  -- 84 days = 12 weeks

-- Classify the seasonality
WITH values_list AS (
    SELECT LIST(value ORDER BY date) AS values
    FROM weekly_seasonal
)
SELECT
    (ts_classify_seasonality(values, 7.0)).timing_classification AS timing_class,
    (ts_classify_seasonality(values, 7.0)).modulation_type AS modulation,
    (ts_classify_seasonality(values, 7.0)).is_seasonal AS is_seasonal,
    (ts_classify_seasonality(values, 7.0)).has_stable_timing AS stable_timing,
    ROUND((ts_classify_seasonality(values, 7.0)).seasonal_strength, 4) AS strength,
    ROUND((ts_classify_seasonality(values, 7.0)).timing_variability, 4) AS variability
FROM values_list;

-- Section 1.2: Generate non-seasonal (random) data
SELECT 'Section 1.2: Non-Seasonal (Random) Pattern' AS step;

CREATE OR REPLACE TABLE random_data AS
SELECT
    '2024-01-01'::DATE + (i * INTERVAL '1 day') AS date,
    i + 1 AS day_index,
    ROUND(1000.0 + (RANDOM() - 0.5) * 500, 2) AS value
FROM generate_series(0, 83) AS t(i);

WITH values_list AS (
    SELECT LIST(value ORDER BY date) AS values
    FROM random_data
)
SELECT
    (ts_classify_seasonality(values, 7.0)).timing_classification AS timing_class,
    (ts_classify_seasonality(values, 7.0)).modulation_type AS modulation,
    (ts_classify_seasonality(values, 7.0)).is_seasonal AS is_seasonal,
    (ts_classify_seasonality(values, 7.0)).has_stable_timing AS stable_timing,
    ROUND((ts_classify_seasonality(values, 7.0)).seasonal_strength, 4) AS strength
FROM values_list;

-- Section 1.3: Generate trend-only data (no seasonality)
SELECT 'Section 1.3: Trend-Only Pattern (No Seasonality)' AS step;

CREATE OR REPLACE TABLE trend_data AS
SELECT
    '2024-01-01'::DATE + (i * INTERVAL '1 day') AS date,
    i + 1 AS day_index,
    ROUND(500.0 + i * 10.0 + (RANDOM() - 0.5) * 30, 2) AS value
FROM generate_series(0, 83) AS t(i);

WITH values_list AS (
    SELECT LIST(value ORDER BY date) AS values
    FROM trend_data
)
SELECT
    (ts_classify_seasonality(values, 7.0)).timing_classification AS timing_class,
    (ts_classify_seasonality(values, 7.0)).is_seasonal AS is_seasonal
FROM values_list;

-- Section 1.4: Full classification struct output
SELECT 'Section 1.4: Full Classification Structure' AS step;

WITH values_list AS (
    SELECT LIST(value ORDER BY date) AS values
    FROM weekly_seasonal
)
SELECT ts_classify_seasonality(values, 7.0) AS full_classification
FROM values_list;

-- Section 1.5: Custom threshold parameters
SELECT 'Section 1.5: Custom Threshold Parameters' AS step;

WITH values_list AS (
    SELECT LIST(value ORDER BY date) AS values
    FROM weekly_seasonal
)
SELECT
    -- Default thresholds
    (ts_classify_seasonality(values, 7.0)).is_seasonal AS default_seasonal,
    -- Stricter strength threshold
    (ts_classify_seasonality(values, 7.0, 0.5)).is_seasonal AS strict_seasonal,
    -- Very strict
    (ts_classify_seasonality(values, 7.0, 0.7, 0.05)).is_seasonal AS very_strict_seasonal
FROM values_list;

-- ============================================================================
-- PATTERN 2: Aggregate Seasonality Classification
-- ============================================================================
-- Scenario: Classify seasonality from raw (timestamp, value) pairs
-- Use cases: Group-level analysis, SQL aggregation workflows

SELECT
    '=== Pattern 2: Aggregate Classification ===' AS section;

-- Section 2.1: Direct aggregate classification
SELECT 'Section 2.1: Aggregate Function on Single Series' AS step;

SELECT
    ts_classify_seasonality_agg(date, value, 7.0) AS classification
FROM weekly_seasonal;

-- Section 2.2: Access specific fields from aggregate
SELECT 'Section 2.2: Extract Specific Fields from Aggregate' AS step;

SELECT
    (ts_classify_seasonality_agg(date, value, 7.0)).timing_classification AS timing_class,
    (ts_classify_seasonality_agg(date, value, 7.0)).modulation_type AS modulation,
    (ts_classify_seasonality_agg(date, value, 7.0)).is_seasonal AS is_seasonal,
    ROUND((ts_classify_seasonality_agg(date, value, 7.0)).seasonal_strength, 4) AS strength
FROM weekly_seasonal;

-- ============================================================================
-- PATTERN 3: Table Macro for Grouped Analysis
-- ============================================================================
-- Scenario: Classify seasonality for multiple series at once
-- Use cases: Product portfolio analysis, multi-store demand classification

SELECT
    '=== Pattern 3: Table Macro for Grouped Analysis ===' AS section;

-- Section 3.1: Create multi-series dataset
SELECT 'Section 3.1: Create Multi-Series Dataset' AS step;

CREATE OR REPLACE TABLE multi_series AS
-- Series A: Strong weekly seasonality
SELECT
    'series_a' AS series_id,
    '2024-01-01'::TIMESTAMP + (i * INTERVAL '1 day') AS ts,
    ROUND(1000.0 + 400.0 * SIN(2 * PI() * (i % 7) / 7.0) + (RANDOM() - 0.5) * 30, 2) AS value
FROM generate_series(0, 55) AS t(i)
UNION ALL
-- Series B: Weak seasonality
SELECT
    'series_b' AS series_id,
    '2024-01-01'::TIMESTAMP + (i * INTERVAL '1 day') AS ts,
    ROUND(800.0 + 50.0 * SIN(2 * PI() * (i % 7) / 7.0) + (RANDOM() - 0.5) * 100, 2) AS value
FROM generate_series(0, 55) AS t(i)
UNION ALL
-- Series C: No seasonality (random walk)
SELECT
    'series_c' AS series_id,
    '2024-01-01'::TIMESTAMP + (i * INTERVAL '1 day') AS ts,
    ROUND(500.0 + i * 2.0 + (RANDOM() - 0.5) * 80, 2) AS value
FROM generate_series(0, 55) AS t(i);

-- Section 3.2: Classify all series using table macro
SELECT 'Section 3.2: Batch Classification with Table Macro' AS step;

SELECT * FROM ts_classify_seasonality_by('multi_series', series_id, ts, value, 7);

-- Section 3.3: Extract and compare classifications
SELECT 'Section 3.3: Compare Series Classifications' AS step;

SELECT
    id AS series_id,
    (classification).timing_classification AS timing_class,
    (classification).modulation_type AS modulation,
    (classification).is_seasonal AS is_seasonal,
    (classification).has_stable_timing AS stable_timing,
    ROUND((classification).seasonal_strength, 4) AS strength,
    ROUND((classification).timing_variability, 4) AS variability
FROM ts_classify_seasonality_by('multi_series', series_id, ts, value, 7)
ORDER BY series_id;

-- Section 3.4: Filter series by seasonality type
SELECT 'Section 3.4: Filter Seasonal Series Only' AS step;

SELECT
    id AS series_id,
    (classification).timing_classification AS timing_class,
    ROUND((classification).seasonal_strength, 4) AS strength
FROM ts_classify_seasonality_by('multi_series', series_id, ts, value, 7)
WHERE (classification).is_seasonal = true;

-- Section 3.5: Group-level analysis with aggregate
SELECT 'Section 3.5: Group-Level Analysis with Aggregate Function' AS step;

SELECT
    series_id,
    (ts_classify_seasonality_agg(ts, value, 7.0)).timing_classification AS timing_class,
    (ts_classify_seasonality_agg(ts, value, 7.0)).is_seasonal AS is_seasonal,
    ROUND((ts_classify_seasonality_agg(ts, value, 7.0)).seasonal_strength, 4) AS strength
FROM multi_series
GROUP BY series_id
ORDER BY series_id;

-- ============================================================================
-- PATTERN 4: Practical Application - Forecasting Method Selection
-- ============================================================================
-- Scenario: Use classification to select appropriate forecasting method
-- Use cases: Automated forecasting pipelines, method selection logic

SELECT
    '=== Pattern 4: Forecasting Method Selection ===' AS section;

SELECT 'Section 4.1: Recommend Forecasting Method Based on Classification' AS step;

WITH classifications AS (
    SELECT
        id AS series_id,
        classification
    FROM ts_classify_seasonality_by('multi_series', series_id, ts, value, 7)
)
SELECT
    series_id,
    (classification).timing_classification AS timing_class,
    (classification).is_seasonal AS is_seasonal,
    (classification).has_stable_timing AS stable_timing,
    ROUND((classification).seasonal_strength, 4) AS strength,
    CASE
        WHEN NOT (classification).is_seasonal THEN 'AutoARIMA or Theta (non-seasonal)'
        WHEN (classification).has_stable_timing AND (classification).seasonal_strength > 0.5
            THEN 'MSTL or STL decomposition (strong stable seasonality)'
        WHEN (classification).has_stable_timing
            THEN 'ETS with seasonal component (moderate seasonality)'
        ELSE 'AutoARIMA with seasonal differencing (variable seasonality)'
    END AS recommended_method
FROM classifications
ORDER BY series_id;

-- ============================================================================
-- PATTERN 5: Cycle Strength and Weak Season Analysis
-- ============================================================================
-- Scenario: Identify which cycles are weak or anomalous
-- Use cases: Quality control, anomaly detection in seasonal patterns

SELECT
    '=== Pattern 5: Cycle Strength Analysis ===' AS section;

SELECT 'Section 5.1: Analyze Individual Cycle Strengths' AS step;

WITH values_list AS (
    SELECT LIST(value ORDER BY date) AS values
    FROM weekly_seasonal
)
SELECT
    (ts_classify_seasonality(values, 7.0)).cycle_strengths AS cycle_strengths,
    (ts_classify_seasonality(values, 7.0)).weak_seasons AS weak_season_indices,
    LENGTH((ts_classify_seasonality(values, 7.0)).cycle_strengths) AS total_cycles,
    LENGTH((ts_classify_seasonality(values, 7.0)).weak_seasons) AS weak_cycle_count
FROM values_list;

-- ============================================================================
-- CLEANUP
-- ============================================================================

SELECT
    '=== Examples Complete ===' AS section;

-- Optionally drop temporary tables
-- DROP TABLE IF EXISTS weekly_seasonal;
-- DROP TABLE IF EXISTS random_data;
-- DROP TABLE IF EXISTS trend_data;
-- DROP TABLE IF EXISTS multi_series;
