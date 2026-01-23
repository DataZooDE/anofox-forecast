-- ============================================================================
-- Seasonality Classification Examples - Synthetic Data
-- ============================================================================
-- This script demonstrates seasonality classification with the anofox-forecast
-- extension using the ts_classify_seasonality_by table macro.
--
-- Run: ./build/release/duckdb < examples/seasonality_classification/synthetic_seasonality_examples.sql
-- ============================================================================

-- Load extension
LOAD anofox_forecast;
INSTALL json;
LOAD json;

.print '============================================================================='
.print 'SEASONALITY CLASSIFICATION EXAMPLES - Using ts_classify_seasonality_by'
.print '============================================================================='

-- ============================================================================
-- SECTION 1: Basic Seasonality Classification for Multiple Series
-- ============================================================================
-- Use ts_classify_seasonality_by to classify seasonality type across grouped series.

.print ''
.print '>>> SECTION 1: Basic Seasonality Classification'
.print '-----------------------------------------------------------------------------'

-- Create multi-series data with different seasonal patterns
CREATE OR REPLACE TABLE multi_series AS
SELECT * FROM (
    -- Series A: Strong weekly seasonality
    SELECT
        'series_A' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
        ROUND(1000.0 + 400.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 30, 2) AS value
    FROM generate_series(0, 55) AS t(i)
    UNION ALL
    -- Series B: Weak seasonality
    SELECT
        'series_B' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
        ROUND(800.0 + 50.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 100, 2) AS value
    FROM generate_series(0, 55) AS t(i)
    UNION ALL
    -- Series C: No seasonality (random walk)
    SELECT
        'series_C' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
        ROUND(500.0 + i * 2.0 + (RANDOM() - 0.5) * 80, 2) AS value
    FROM generate_series(0, 55) AS t(i)
);

.print 'Multi-series data summary:'
SELECT series_id, COUNT(*) AS n_rows, ROUND(AVG(value), 2) AS avg_value
FROM multi_series GROUP BY series_id ORDER BY series_id;

-- 1.1: Basic classification (all series at once)
.print ''
.print 'Section 1.1: Basic Seasonality Classification (period=7)'

SELECT * FROM ts_classify_seasonality_by('multi_series', series_id, ts, value, 7);

-- ============================================================================
-- SECTION 2: Accessing Classification Results
-- ============================================================================

.print ''
.print '>>> SECTION 2: Accessing Classification Results'
.print '-----------------------------------------------------------------------------'

-- 2.1: Extract specific classification fields
.print 'Section 2.1: Extract Specific Fields'

SELECT
    id AS series_id,
    timing_classification AS timing_class,
    modulation_type AS modulation,
    is_seasonal AS is_seasonal,
    has_stable_timing AS stable_timing,
    ROUND(seasonal_strength, 4) AS strength,
    ROUND(timing_variability, 4) AS variability
FROM ts_classify_seasonality_by('multi_series', series_id, ts, value, 7)
ORDER BY series_id;

-- 2.2: Filter only seasonal series
.print ''
.print 'Section 2.2: Filter Seasonal Series Only'

SELECT
    id AS series_id,
    timing_classification AS timing_class,
    ROUND(seasonal_strength, 4) AS strength
FROM ts_classify_seasonality_by('multi_series', series_id, ts, value, 7)
WHERE is_seasonal = true;

-- ============================================================================
-- SECTION 3: Different Seasonal Patterns
-- ============================================================================

.print ''
.print '>>> SECTION 3: Different Seasonal Patterns'
.print '-----------------------------------------------------------------------------'

-- Create series with different seasonal behaviors
CREATE OR REPLACE TABLE varied_patterns AS
SELECT * FROM (
    -- Stable seasonal: Regular weekly pattern
    SELECT
        'stable_seasonal' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
        1000.0 + 400.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 20 AS value
    FROM generate_series(0, 83) AS t(i)
    UNION ALL
    -- Variable seasonal: Pattern strength varies
    SELECT
        'variable_seasonal' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
        1000.0 + (200.0 + 200.0 * (i / 84.0)) * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 30 AS value
    FROM generate_series(0, 83) AS t(i)
    UNION ALL
    -- Intermittent seasonal: Seasonality comes and goes
    SELECT
        'intermittent' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
        1000.0 + CASE WHEN i % 28 < 14 THEN 300.0 * SIN(2 * PI() * i / 7.0) ELSE 0 END + (RANDOM() - 0.5) * 50 AS value
    FROM generate_series(0, 83) AS t(i)
    UNION ALL
    -- Non-seasonal: Trend only
    SELECT
        'non_seasonal' AS series_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS ts,
        500.0 + i * 5.0 + (RANDOM() - 0.5) * 40 AS value
    FROM generate_series(0, 83) AS t(i)
);

.print 'Section 3.1: Classification of Different Patterns'

SELECT
    id AS pattern_type,
    timing_classification AS timing_class,
    modulation_type AS modulation,
    is_seasonal AS is_seasonal,
    has_stable_timing AS stable_timing,
    ROUND(seasonal_strength, 4) AS strength
FROM ts_classify_seasonality_by('varied_patterns', series_id, ts, value, 7)
ORDER BY id;

-- ============================================================================
-- SECTION 4: Forecasting Method Selection
-- ============================================================================

.print ''
.print '>>> SECTION 4: Forecasting Method Selection Based on Classification'
.print '-----------------------------------------------------------------------------'

.print 'Section 4.1: Recommend Forecasting Method Based on Classification'

SELECT
    id AS series_id,
    timing_classification AS timing_class,
    is_seasonal AS is_seasonal,
    has_stable_timing AS stable_timing,
    ROUND(seasonal_strength, 4) AS strength,
    CASE
        WHEN NOT is_seasonal THEN 'AutoARIMA or Theta (non-seasonal)'
        WHEN has_stable_timing AND seasonal_strength > 0.5
            THEN 'MSTL or STL decomposition (strong stable seasonality)'
        WHEN has_stable_timing
            THEN 'ETS with seasonal component (moderate seasonality)'
        ELSE 'AutoARIMA with seasonal differencing (variable seasonality)'
    END AS recommended_method
FROM ts_classify_seasonality_by('multi_series', series_id, ts, value, 7)
ORDER BY series_id;

-- ============================================================================
-- SECTION 5: Real-World Scenarios
-- ============================================================================

.print ''
.print '>>> SECTION 5: Real-World Scenarios'
.print '-----------------------------------------------------------------------------'

-- Create retail sales data with different seasonal behaviors
CREATE OR REPLACE TABLE retail_sales AS
SELECT * FROM (
    -- Store A: Strong weekly pattern (weekend peaks)
    SELECT
        'Store_A' AS store_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        ROUND(
            5000.0
            + 1500.0 * SIN(2 * PI() * i / 7.0)
            + (RANDOM() - 0.5) * 200
        , 0)::DOUBLE AS sales
    FROM generate_series(0, 83) AS t(i)
    UNION ALL
    -- Store B: Weak weekly pattern (online store)
    SELECT
        'Store_B' AS store_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        ROUND(
            3000.0
            + 200.0 * SIN(2 * PI() * i / 7.0)
            + i * 10  -- growth trend
            + (RANDOM() - 0.5) * 300
        , 0)::DOUBLE AS sales
    FROM generate_series(0, 83) AS t(i)
    UNION ALL
    -- Store C: No weekly pattern (B2B)
    SELECT
        'Store_C' AS store_id,
        '2024-01-01'::TIMESTAMP + INTERVAL (i) DAY AS date,
        ROUND(
            4000.0
            + i * 5
            + (RANDOM() - 0.5) * 400
        , 0)::DOUBLE AS sales
    FROM generate_series(0, 83) AS t(i)
);

.print 'Section 5.1: Retail Sales Seasonality Classification'

SELECT
    id AS store,
    timing_classification AS timing_class,
    is_seasonal AS is_seasonal,
    ROUND(seasonal_strength, 4) AS strength,
    CASE
        WHEN seasonal_strength > 0.5 THEN 'Strong weekly pattern'
        WHEN is_seasonal THEN 'Weak weekly pattern'
        ELSE 'No weekly pattern'
    END AS pattern_description
FROM ts_classify_seasonality_by('retail_sales', store_id, date, sales, 7)
ORDER BY strength DESC;

-- ============================================================================
-- SECTION 6: Cycle Strength Analysis
-- ============================================================================

.print ''
.print '>>> SECTION 6: Cycle Strength Analysis'
.print '-----------------------------------------------------------------------------'

.print 'Section 6.1: Analyze Individual Cycle Strengths'

SELECT
    id AS series_id,
    cycle_strengths AS cycle_strengths,
    weak_seasons AS weak_season_indices,
    list_count(cycle_strengths) AS total_cycles,
    list_count(weak_seasons) AS weak_cycle_count
FROM ts_classify_seasonality_by('multi_series', series_id, ts, value, 7)
ORDER BY series_id;

-- ============================================================================
-- CLEANUP
-- ============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS multi_series;
DROP TABLE IF EXISTS varied_patterns;
DROP TABLE IF EXISTS retail_sales;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'SEASONALITY CLASSIFICATION EXAMPLES COMPLETE'
.print '============================================================================='
