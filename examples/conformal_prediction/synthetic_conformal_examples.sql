-- ============================================================================
-- Conformal Prediction Examples - Synthetic Data
-- ============================================================================
-- This script demonstrates conformal prediction with the anofox-forecast
-- extension using *_by table macros.
--
-- Run: ./build/release/duckdb < examples/conformal_prediction/synthetic_conformal_examples.sql
-- ============================================================================

-- Load extension
LOAD anofox_forecast;
INSTALL json;
LOAD json;

.print '============================================================================='
.print 'CONFORMAL PREDICTION EXAMPLES - Using *_by Table Macros'
.print '============================================================================='

-- ============================================================================
-- SECTION 1: Basic Conformal Prediction for Multiple Series
-- ============================================================================
-- Use ts_conformal_by to compute prediction intervals from backtest data.

.print ''
.print '>>> SECTION 1: Basic Conformal Prediction'
.print '-----------------------------------------------------------------------------'

-- Create multi-series backtest data with actuals, historical forecasts, and new forecasts
CREATE OR REPLACE TABLE backtest_with_forecasts AS
SELECT * FROM (
    -- Product A: Good forecast quality
    SELECT
        'Product_A' AS product_id,
        i AS period,
        100.0 + i * 2.0 + (RANDOM() - 0.5) * 10 AS actual,
        100.0 + i * 2.0 + (RANDOM() - 0.5) * 5 AS forecast,
        NULL::DOUBLE AS point_forecast
    FROM generate_series(1, 40) AS t(i)
    UNION ALL
    -- Product A: New point forecasts to add intervals to
    SELECT
        'Product_A' AS product_id,
        i AS period,
        NULL AS actual,
        NULL AS forecast,
        180.0 + (i - 40) * 2.0 AS point_forecast
    FROM generate_series(41, 45) AS t(i)
    UNION ALL
    -- Product B: More volatile
    SELECT
        'Product_B' AS product_id,
        i AS period,
        200.0 + i * 3.0 + (RANDOM() - 0.5) * 20 AS actual,
        200.0 + i * 3.0 + (RANDOM() - 0.5) * 15 AS forecast,
        NULL::DOUBLE AS point_forecast
    FROM generate_series(1, 40) AS t(i)
    UNION ALL
    -- Product B: New point forecasts
    SELECT
        'Product_B' AS product_id,
        i AS period,
        NULL AS actual,
        NULL AS forecast,
        320.0 + (i - 40) * 3.0 AS point_forecast
    FROM generate_series(41, 45) AS t(i)
    UNION ALL
    -- Product C: Biased forecast
    SELECT
        'Product_C' AS product_id,
        i AS period,
        150.0 + i * 1.5 + (RANDOM() - 0.5) * 8 AS actual,
        140.0 + i * 1.5 + (RANDOM() - 0.5) * 5 AS forecast,
        NULL::DOUBLE AS point_forecast
    FROM generate_series(1, 40) AS t(i)
    UNION ALL
    -- Product C: New point forecasts
    SELECT
        'Product_C' AS product_id,
        i AS period,
        NULL AS actual,
        NULL AS forecast,
        200.0 + (i - 40) * 1.5 AS point_forecast
    FROM generate_series(41, 45) AS t(i)
);

.print 'Backtest data summary:'
SELECT
    product_id,
    COUNT(*) FILTER (WHERE actual IS NOT NULL) AS n_backtest,
    COUNT(*) FILTER (WHERE point_forecast IS NOT NULL) AS n_forecasts,
    ROUND(AVG(actual) FILTER (WHERE actual IS NOT NULL), 2) AS avg_actual
FROM backtest_with_forecasts
GROUP BY product_id
ORDER BY product_id;

-- 1.1: Basic conformal prediction (90% coverage)
.print ''
.print 'Section 1.1: Conformal Prediction Intervals (90% coverage)'

SELECT * FROM ts_conformal_by('backtest_with_forecasts', product_id, actual, forecast, point_forecast, MAP{});

-- ============================================================================
-- SECTION 2: Conformal with Different Alpha Levels
-- ============================================================================

.print ''
.print '>>> SECTION 2: Different Coverage Levels'
.print '-----------------------------------------------------------------------------'

-- 2.1: 95% coverage (alpha=0.05)
.print 'Section 2.1: 95% Coverage Intervals'

SELECT * FROM ts_conformal_by('backtest_with_forecasts', product_id, actual, forecast, point_forecast,
    MAP{'alpha': '0.05'});

-- 2.2: 80% coverage (alpha=0.20)
.print ''
.print 'Section 2.2: 80% Coverage Intervals'

SELECT * FROM ts_conformal_by('backtest_with_forecasts', product_id, actual, forecast, point_forecast,
    MAP{'alpha': '0.20'});

-- ============================================================================
-- SECTION 3: Asymmetric Intervals
-- ============================================================================

.print ''
.print '>>> SECTION 3: Asymmetric Intervals'
.print '-----------------------------------------------------------------------------'

-- Create data with skewed residuals
CREATE OR REPLACE TABLE skewed_backtest AS
SELECT * FROM (
    -- Series with positive skew (over-forecasting more than under)
    SELECT
        'Skewed_Series' AS series_id,
        i AS period,
        100.0 + i + CASE WHEN RANDOM() > 0.3 THEN (RANDOM() * 20) ELSE -(RANDOM() * 5) END AS actual,
        100.0 + i AS forecast,
        NULL::DOUBLE AS point_forecast
    FROM generate_series(1, 50) AS t(i)
    UNION ALL
    SELECT
        'Skewed_Series' AS series_id,
        i AS period,
        NULL AS actual,
        NULL AS forecast,
        150.0 + (i - 50) AS point_forecast
    FROM generate_series(51, 55) AS t(i)
);

-- 3.1: Asymmetric conformal prediction
.print 'Section 3.1: Asymmetric Intervals for Skewed Residuals'

SELECT * FROM ts_conformal_by('skewed_backtest', series_id, actual, forecast, point_forecast,
    MAP{'asymmetric': 'true'});

-- ============================================================================
-- SECTION 4: Real-World Scenarios
-- ============================================================================

.print ''
.print '>>> SECTION 4: Real-World Scenarios'
.print '-----------------------------------------------------------------------------'

-- 4.1: Retail demand forecasting
.print 'Section 4.1: Retail Demand Forecasting'

CREATE OR REPLACE TABLE retail_backtest AS
SELECT * FROM (
    -- Store A: Consistent demand
    SELECT
        'Store_A' AS store_id,
        DATE '2024-01-01' + INTERVAL (i - 1) DAY AS date,
        i AS period,
        ROUND(500.0 + 10 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 50, 0)::DOUBLE AS actual_demand,
        ROUND(500.0 + 10 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 20, 0)::DOUBLE AS forecast_demand,
        NULL::DOUBLE AS new_forecast
    FROM generate_series(1, 60) AS t(i)
    UNION ALL
    SELECT
        'Store_A' AS store_id,
        DATE '2024-03-01' + INTERVAL (i - 60 - 1) DAY AS date,
        i AS period,
        NULL AS actual_demand,
        NULL AS forecast_demand,
        ROUND(520.0 + 10 * SIN(2 * PI() * i / 7), 0)::DOUBLE AS new_forecast
    FROM generate_series(61, 67) AS t(i)
    UNION ALL
    -- Store B: Higher volume, more variance
    SELECT
        'Store_B' AS store_id,
        DATE '2024-01-01' + INTERVAL (i - 1) DAY AS date,
        i AS period,
        ROUND(2000.0 + 100 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 200, 0)::DOUBLE AS actual_demand,
        ROUND(2000.0 + 100 * SIN(2 * PI() * i / 7) + (RANDOM() - 0.5) * 100, 0)::DOUBLE AS forecast_demand,
        NULL::DOUBLE AS new_forecast
    FROM generate_series(1, 60) AS t(i)
    UNION ALL
    SELECT
        'Store_B' AS store_id,
        DATE '2024-03-01' + INTERVAL (i - 60 - 1) DAY AS date,
        i AS period,
        NULL AS actual_demand,
        NULL AS forecast_demand,
        ROUND(2100.0 + 100 * SIN(2 * PI() * i / 7), 0)::DOUBLE AS new_forecast
    FROM generate_series(61, 67) AS t(i)
);

.print 'Store-specific prediction intervals:'
SELECT * FROM ts_conformal_by('retail_backtest', store_id, actual_demand, forecast_demand, new_forecast, MAP{});

-- ============================================================================
-- SECTION 5: Using ts_conformal_apply_by with Pre-Computed Score
-- ============================================================================

.print ''
.print '>>> SECTION 5: Apply Pre-Computed Conformity Score'
.print '-----------------------------------------------------------------------------'

-- Create forecast table
CREATE OR REPLACE TABLE future_forecasts AS
SELECT
    product_id,
    period,
    point_forecast
FROM (
    VALUES
        ('Product_A', 46, 190.0), ('Product_A', 47, 192.0), ('Product_A', 48, 194.0),
        ('Product_B', 46, 330.0), ('Product_B', 47, 333.0), ('Product_B', 48, 336.0),
        ('Product_C', 46, 207.5), ('Product_C', 47, 209.0), ('Product_C', 48, 210.5)
) AS t(product_id, period, point_forecast);

-- Apply a known conformity score (e.g., from previous calibration)
.print 'Applying conformity score of 15.0 to all series:'

SELECT * FROM ts_conformal_apply_by('future_forecasts', product_id, point_forecast, 15.0);

-- ============================================================================
-- SECTION 6: Comparing Interval Widths Across Series
-- ============================================================================

.print ''
.print '>>> SECTION 6: Comparing Interval Widths'
.print '-----------------------------------------------------------------------------'

.print 'Interval width reflects forecast uncertainty per series:'

WITH conformal_results AS (
    SELECT * FROM ts_conformal_by('backtest_with_forecasts', product_id, actual, forecast, point_forecast, MAP{})
)
SELECT
    group_col AS product,
    ROUND(conformity_score, 2) AS score,
    ROUND(list_avg(upper) - list_avg(lower), 2) AS avg_interval_width,
    CASE
        WHEN conformity_score < 10 THEN 'Narrow (good accuracy)'
        WHEN conformity_score < 20 THEN 'Medium'
        ELSE 'Wide (high uncertainty)'
    END AS uncertainty_level
FROM conformal_results
ORDER BY conformity_score;

-- ============================================================================
-- Note: ts_conformal_by returns group_col, not id
-- ============================================================================

-- ============================================================================
-- CLEANUP
-- ============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS backtest_with_forecasts;
DROP TABLE IF EXISTS skewed_backtest;
DROP TABLE IF EXISTS retail_backtest;
DROP TABLE IF EXISTS future_forecasts;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'CONFORMAL PREDICTION EXAMPLES COMPLETE'
.print '============================================================================='
