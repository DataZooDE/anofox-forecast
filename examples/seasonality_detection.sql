-- ============================================================================
-- SEASONALITY DETECTION EXAMPLE
-- ============================================================================
-- Demonstrates how to use TS_DETECT_SEASONALITY and TS_ANALYZE_SEASONALITY
-- to automatically detect seasonal patterns in time series data
-- ============================================================================

.timer on

LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- ============================================================================
-- STEP 1: Create time series with known seasonality
-- ============================================================================
SELECT '=== STEP 1: Creating time series with weekly seasonality ===' AS step;

DROP TABLE IF EXISTS seasonal_data;
CREATE TABLE seasonal_data AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS ds,
    -- Weekly pattern (period=7)
    100 + 20 * SIN(2 * PI() * d / 7) +
    -- Monthly pattern (period=30)
    15 * COS(2 * PI() * d / 30) +
    -- Random noise
    (RANDOM() * 5 - 2.5) AS y
FROM generate_series(0, 364) t(d);

SELECT * FROM seasonal_data LIMIT 10;

-- ============================================================================
-- STEP 2: Simple seasonality detection (TS_DETECT_SEASONALITY)
-- ============================================================================
SELECT '=== STEP 2: Detect seasonal periods ===' AS step;

-- Aggregate values into array and detect seasonality
WITH aggregated AS (
    SELECT 
        LIST(y ORDER BY ds) AS values
    FROM seasonal_data
)
SELECT 
    TS_DETECT_SEASONALITY(values) AS detected_periods
FROM aggregated;

-- ============================================================================
-- STEP 3: Detailed seasonality analysis (TS_ANALYZE_SEASONALITY)
-- ============================================================================
SELECT '=== STEP 3: Detailed seasonality analysis ===' AS step;

-- Full analysis with timestamps and values
WITH aggregated AS (
    SELECT 
        LIST(ds ORDER BY ds) AS timestamps,
        LIST(y ORDER BY ds) AS values
    FROM seasonal_data
)
SELECT 
    analysis.*
FROM aggregated,
LATERAL (SELECT TS_ANALYZE_SEASONALITY(timestamps, values) AS analysis) a;

-- Extract individual components
WITH aggregated AS (
    SELECT 
        LIST(ds ORDER BY ds) AS timestamps,
        LIST(y ORDER BY ds) AS values
    FROM seasonal_data
),
analysis AS (
    SELECT TS_ANALYZE_SEASONALITY(timestamps, values) AS result
    FROM aggregated
)
SELECT 
    result.detected_periods AS periods,
    result.primary_period AS primary,
    ROUND(result.seasonal_strength, 3) AS seasonal_strength,
    ROUND(result.trend_strength, 3) AS trend_strength
FROM analysis;

-- ============================================================================
-- STEP 4: Seasonality detection for multiple series
-- ============================================================================
SELECT '=== STEP 4: Detect seasonality for multiple series ===' AS step;

DROP TABLE IF EXISTS multi_seasonal;
CREATE TABLE multi_seasonal AS
WITH RECURSIVE
    series_ids AS (
        SELECT 1 AS id
        UNION ALL
        SELECT id + 1 FROM series_ids WHERE id < 5
    ),
    dates AS (
        SELECT DATE '2023-01-01' + INTERVAL (d) DAY AS ds
        FROM generate_series(0, 364) t(d)
    )
SELECT 
    id,
    ds,
    -- Different seasonality patterns for different series
    100 + 
    CASE 
        WHEN id = 1 THEN 20 * SIN(2 * PI() * EXTRACT(DAY FROM ds) / 7)  -- Weekly
        WHEN id = 2 THEN 20 * SIN(2 * PI() * EXTRACT(DAY FROM ds) / 14) -- Bi-weekly
        WHEN id = 3 THEN 20 * SIN(2 * PI() * EXTRACT(DAY FROM ds) / 30) -- Monthly
        WHEN id = 4 THEN 20 * SIN(2 * PI() * EXTRACT(DAY FROM ds) / 7) + 
                         10 * COS(2 * PI() * EXTRACT(DAY FROM ds) / 30)  -- Mixed
        ELSE (RANDOM() * 20)  -- No seasonality
    END + (RANDOM() * 5) AS y
FROM series_ids, dates;

SELECT id, COUNT(*) AS num_points, 
       ROUND(AVG(y), 2) AS avg_value,
       ROUND(STDDEV(y), 2) AS stddev
FROM multi_seasonal
GROUP BY id
ORDER BY id;

-- Detect seasonality for each series
WITH aggregated AS (
    SELECT 
        id,
        LIST(y ORDER BY ds) AS values
    FROM multi_seasonal
    GROUP BY id
)
SELECT 
    id,
    TS_DETECT_SEASONALITY(values) AS detected_periods
FROM aggregated
ORDER BY id;

-- ============================================================================
-- STEP 5: Full analysis for each series
-- ============================================================================
SELECT '=== STEP 5: Full seasonality analysis per series ===' AS step;

WITH aggregated AS (
    SELECT 
        id,
        LIST(ds ORDER BY ds) AS timestamps,
        LIST(y ORDER BY ds) AS values
    FROM multi_seasonal
    GROUP BY id
),
analysis AS (
    SELECT 
        id,
        TS_ANALYZE_SEASONALITY(timestamps, values) AS result
    FROM aggregated
)
SELECT 
    id,
    result.detected_periods AS periods,
    result.primary_period AS primary_period,
    ROUND(result.seasonal_strength, 3) AS seasonal_str,
    ROUND(result.trend_strength, 3) AS trend_str
FROM analysis
ORDER BY id;

-- ============================================================================
-- STEP 6: Using detected seasonality for forecasting
-- ============================================================================
SELECT '=== STEP 6: Forecast using auto-detected seasonality ===' AS step;

-- Detect seasonality and use it for forecasting
WITH aggregated AS (
    SELECT 
        LIST(y ORDER BY ds) AS values
    FROM seasonal_data
),
detected AS (
    SELECT TS_DETECT_SEASONALITY(values) AS periods
    FROM aggregated
)
SELECT 
    'Detected periods: ' || ARRAY_TO_STRING(periods, ', ') AS info
FROM detected;

-- Use the detected period (17) for forecasting
SELECT 
    forecast_step,
    date_col AS forecast_date,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(lower, 2) AS lower_bound,
    ROUND(upper, 2) AS upper_bound
FROM TS_FORECAST(
    'seasonal_data', 
    ds, 
    y, 
    'SeasonalNaive', 
    28,
    {'seasonal_period': 17}  -- Using the detected primary period
)
LIMIT 14;

-- ============================================================================
-- STEP 7: Comparing seasonality across different time windows
-- ============================================================================
SELECT '=== STEP 7: Seasonality detection in rolling windows ===' AS step;

WITH windows AS (
    SELECT 
        CASE 
            WHEN ds < '2023-04-01' THEN 'Q1'
            WHEN ds < '2023-07-01' THEN 'Q2'
            WHEN ds < '2023-10-01' THEN 'Q3'
            ELSE 'Q4'
        END AS quarter,
        y
    FROM seasonal_data
),
aggregated AS (
    SELECT 
        quarter,
        LIST(y) AS values
    FROM windows
    GROUP BY quarter
)
SELECT 
    quarter,
    TS_DETECT_SEASONALITY(values) AS detected_periods
FROM aggregated
ORDER BY quarter;

.timer off

-- ============================================================================
-- KEY TAKEAWAYS
-- ============================================================================
-- 1. TS_DETECT_SEASONALITY: Quick detection of seasonal periods from values
-- 2. TS_ANALYZE_SEASONALITY: Comprehensive analysis including:
--    - Detected periods (up to 3 strongest)
--    - Primary period (strongest one)
--    - Seasonal strength (0-1, how strong is seasonality)
--    - Trend strength (0-1, how strong is the trend)
-- 3. Use detected seasonality to automatically configure forecasting models
-- 4. Works with GROUP BY to detect seasonality for multiple series
-- 5. Can analyze seasonality in different time windows
-- ============================================================================

