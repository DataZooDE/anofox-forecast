-- ============================================================================
-- Metrics Examples - Synthetic Data
-- ============================================================================
-- This script demonstrates forecast accuracy metrics with the anofox-forecast
-- extension using *_by table macros.
--
-- Run: ./build/release/duckdb < examples/metrics/synthetic_metrics_examples.sql
-- ============================================================================

-- Load extension
LOAD anofox_forecast;
INSTALL json;
LOAD json;

.print '============================================================================='
.print 'METRICS EXAMPLES - Using *_by Table Macros'
.print '============================================================================='

-- ============================================================================
-- SECTION 1: Basic Point Metrics for Multiple Series
-- ============================================================================
-- Use ts_*_by macros to evaluate forecast accuracy across grouped series.

.print ''
.print '>>> SECTION 1: Basic Point Metrics'
.print '-----------------------------------------------------------------------------'

-- Create multi-series forecast evaluation data
CREATE OR REPLACE TABLE forecast_results AS
SELECT * FROM (
    -- Product A: Good forecast quality
    SELECT
        'Product_A' AS product_id,
        DATE '2024-01-01' + INTERVAL (i) DAY AS date,
        100.0 + i * 2.0 + (RANDOM() - 0.5) * 10 AS actual,
        100.0 + i * 2.0 + (RANDOM() - 0.5) * 5 AS predicted
    FROM generate_series(1, 20) AS t(i)
    UNION ALL
    -- Product B: Poor forecast quality (higher errors)
    SELECT
        'Product_B' AS product_id,
        DATE '2024-01-01' + INTERVAL (i) DAY AS date,
        200.0 + i * 3.0 + (RANDOM() - 0.5) * 20 AS actual,
        200.0 + i * 3.0 + (RANDOM() - 0.5) * 30 AS predicted
    FROM generate_series(1, 20) AS t(i)
    UNION ALL
    -- Product C: Biased forecast (consistently under-predicting)
    SELECT
        'Product_C' AS product_id,
        DATE '2024-01-01' + INTERVAL (i) DAY AS date,
        150.0 + i * 1.5 + (RANDOM() - 0.5) * 8 AS actual,
        140.0 + i * 1.5 + (RANDOM() - 0.5) * 5 AS predicted
    FROM generate_series(1, 20) AS t(i)
);

.print 'Forecast results summary:'
SELECT product_id, COUNT(*) AS n_points, ROUND(AVG(actual), 2) AS mean_actual
FROM forecast_results GROUP BY product_id ORDER BY product_id;

-- 1.1: MAE (Mean Absolute Error) per series
.print ''
.print 'Section 1.1: Mean Absolute Error (MAE)'

SELECT * FROM ts_mae_by('forecast_results', product_id, date, actual, predicted);

-- 1.2: RMSE (Root Mean Squared Error) per series
.print ''
.print 'Section 1.2: Root Mean Squared Error (RMSE)'

SELECT * FROM ts_rmse_by('forecast_results', product_id, date, actual, predicted);

-- 1.3: MSE (Mean Squared Error) per series
.print ''
.print 'Section 1.3: Mean Squared Error (MSE)'

SELECT * FROM ts_mse_by('forecast_results', product_id, date, actual, predicted);

-- ============================================================================
-- SECTION 2: Percentage Metrics
-- ============================================================================

.print ''
.print '>>> SECTION 2: Percentage Metrics'
.print '-----------------------------------------------------------------------------'

-- 2.1: MAPE (Mean Absolute Percentage Error)
.print 'Section 2.1: Mean Absolute Percentage Error (MAPE)'

SELECT * FROM ts_mape_by('forecast_results', product_id, date, actual, predicted);

-- 2.2: SMAPE (Symmetric Mean Absolute Percentage Error)
.print ''
.print 'Section 2.2: Symmetric MAPE (SMAPE)'

SELECT * FROM ts_smape_by('forecast_results', product_id, date, actual, predicted);

.print ''
.print 'Note: SMAPE is bounded [0, 200%], while MAPE can exceed 100%'

-- ============================================================================
-- SECTION 3: Additional Metrics
-- ============================================================================

.print ''
.print '>>> SECTION 3: Additional Metrics'
.print '-----------------------------------------------------------------------------'

-- 3.1: R-squared
.print 'Section 3.1: R-squared (Coefficient of Determination)'

SELECT * FROM ts_r2_by('forecast_results', product_id, date, actual, predicted);

-- 3.2: Bias (Mean Error)
.print ''
.print 'Section 3.2: Bias (Mean Error)'

SELECT * FROM ts_bias_by('forecast_results', product_id, date, actual, predicted);

.print ''
.print 'Note: Negative bias = under-predicting, Positive bias = over-predicting'

-- ============================================================================
-- SECTION 4: Interval Metrics
-- ============================================================================

.print ''
.print '>>> SECTION 4: Interval Metrics'
.print '-----------------------------------------------------------------------------'

-- Create data with prediction intervals
CREATE OR REPLACE TABLE interval_forecasts AS
SELECT * FROM (
    -- Product A: Well-calibrated intervals
    SELECT
        'Product_A' AS product_id,
        DATE '2024-01-01' + INTERVAL (i) DAY AS date,
        100.0 + (RANDOM() - 0.5) * 20 AS actual,
        90.0 + (RANDOM() - 0.5) * 5 AS lower_90,
        110.0 + (RANDOM() - 0.5) * 5 AS upper_90
    FROM generate_series(1, 30) AS t(i)
    UNION ALL
    -- Product B: Too narrow intervals
    SELECT
        'Product_B' AS product_id,
        DATE '2024-01-01' + INTERVAL (i) DAY AS date,
        100.0 + (RANDOM() - 0.5) * 30 AS actual,
        95.0 + (RANDOM() - 0.5) * 2 AS lower_90,
        105.0 + (RANDOM() - 0.5) * 2 AS upper_90
    FROM generate_series(1, 30) AS t(i)
    UNION ALL
    -- Product C: Too wide intervals
    SELECT
        'Product_C' AS product_id,
        DATE '2024-01-01' + INTERVAL (i) DAY AS date,
        100.0 + (RANDOM() - 0.5) * 10 AS actual,
        70.0 + (RANDOM() - 0.5) * 5 AS lower_90,
        130.0 + (RANDOM() - 0.5) * 5 AS upper_90
    FROM generate_series(1, 30) AS t(i)
);

-- 4.1: Coverage (what % of actuals fall within intervals)
.print 'Section 4.1: Prediction Interval Coverage'

SELECT * FROM ts_coverage_by('interval_forecasts', product_id, date, actual, lower_90, upper_90);

.print ''
.print 'Note: Target coverage for 90% interval should be ~0.90'

-- 4.2: Interval Width
.print ''
.print 'Section 4.2: Mean Interval Width'

SELECT * FROM ts_interval_width_by('interval_forecasts', product_id, lower_90, upper_90);

-- ============================================================================
-- SECTION 5: Comparing Multiple Models
-- ============================================================================

.print ''
.print '>>> SECTION 5: Comparing Multiple Models'
.print '-----------------------------------------------------------------------------'

-- Create multi-model backtest results
CREATE OR REPLACE TABLE model_backtest AS
SELECT * FROM (
    -- Naive model results
    SELECT
        'Product_A' AS product_id,
        'Naive' AS model,
        DATE '2024-01-01' + INTERVAL (i) DAY AS date,
        100.0 + i * 2.0 + 10.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 5 AS actual,
        100.0 + i * 2.0 AS predicted  -- Trend only, no seasonality
    FROM generate_series(1, 28) AS t(i)
    UNION ALL
    -- MSTL model results
    SELECT
        'Product_A' AS product_id,
        'MSTL' AS model,
        DATE '2024-01-01' + INTERVAL (i) DAY AS date,
        100.0 + i * 2.0 + 10.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 5 AS actual,
        100.0 + i * 2.0 + 10.0 * SIN(2 * PI() * i / 7.0) + (RANDOM() - 0.5) * 2 AS predicted  -- Captures seasonality
    FROM generate_series(1, 28) AS t(i)
);

.print 'Model comparison for Product A:'

-- Compare MAE across models
SELECT * FROM ts_mae_by('model_backtest', model, date, actual, predicted);

.print ''
.print 'MSTL should have lower MAE because it captures weekly seasonality'

-- ============================================================================
-- SECTION 6: Combined Metrics Summary
-- ============================================================================

.print ''
.print '>>> SECTION 6: Combined Metrics Summary'
.print '-----------------------------------------------------------------------------'

.print 'All metrics for forecast_results:'

WITH mae AS (SELECT * FROM ts_mae_by('forecast_results', product_id, date, actual, predicted)),
rmse AS (SELECT * FROM ts_rmse_by('forecast_results', product_id, date, actual, predicted)),
mape AS (SELECT * FROM ts_mape_by('forecast_results', product_id, date, actual, predicted)),
bias AS (SELECT * FROM ts_bias_by('forecast_results', product_id, date, actual, predicted))
SELECT
    mae.id AS product,
    ROUND(mae.mae, 2) AS mae,
    ROUND(rmse.rmse, 2) AS rmse,
    ROUND(mape.mape, 2) AS mape_pct,
    ROUND(bias.bias, 2) AS bias,
    CASE
        WHEN ABS(bias.bias) < 2 THEN 'Unbiased'
        WHEN bias.bias < 0 THEN 'Under-predicting'
        ELSE 'Over-predicting'
    END AS bias_assessment
FROM mae
JOIN rmse ON mae.id = rmse.id
JOIN mape ON mae.id = mape.id
JOIN bias ON mae.id = bias.id
ORDER BY mae.mae;

-- ============================================================================
-- CLEANUP
-- ============================================================================

.print ''
.print '>>> CLEANUP'
.print '-----------------------------------------------------------------------------'

DROP TABLE IF EXISTS forecast_results;
DROP TABLE IF EXISTS interval_forecasts;
DROP TABLE IF EXISTS model_backtest;

.print 'All tables cleaned up.'
.print ''
.print '============================================================================='
.print 'METRICS EXAMPLES COMPLETE'
.print '============================================================================='
