-- ============================================================================
-- M5 Backtesting Examples
-- ============================================================================
-- This file demonstrates backtesting on the M5 competition dataset.
-- The M5 dataset contains daily sales data for Walmart products.
--
-- Examples included:
--   1. Basic univariate backtest with ts_backtest_auto
--   2. Model comparison across multiple methods
--   3. Regression backtest with engineered features (requires anofox-statistics)
--
-- Note: We use a subset of the data (10 items) to keep execution time reasonable.
-- ============================================================================

-- Load the extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Load httpfs for remote data access
INSTALL httpfs;
LOAD httpfs;

-- ============================================================================
-- SECTION 1: Load M5 Data (subset for faster execution)
-- ============================================================================

-- Load full M5 dataset
CREATE OR REPLACE TABLE m5_full AS
SELECT
    item_id,
    CAST(timestamp AS TIMESTAMP) AS ds,
    demand AS y
FROM 'https://m5-benchmarks.s3.amazonaws.com/data/train/target.parquet'
ORDER BY item_id, timestamp;

-- Create a subset with 10 items for faster backtesting
CREATE OR REPLACE TABLE m5_sample AS
SELECT * FROM m5_full
WHERE item_id IN (
    SELECT DISTINCT item_id FROM m5_full ORDER BY item_id LIMIT 10
);

-- Show sample data info
SELECT
    'M5 Sample Dataset' AS dataset,
    COUNT(DISTINCT item_id) AS n_items,
    COUNT(*) AS n_rows,
    MIN(ds)::DATE AS start_date,
    MAX(ds)::DATE AS end_date
FROM m5_sample;

-- ============================================================================
-- SECTION 2: Basic Backtest with ts_backtest_auto
-- ============================================================================

-- Run a simple backtest with SeasonalNaive
-- 3 folds, 14-day horizon, weekly seasonality
SELECT
    '=== Basic Backtest: SeasonalNaive ===' AS section;

SELECT
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(AVG(abs_error), 2) AS mae,
    ROUND(fold_metric_score, 4) AS rmse,
    model_name
FROM ts_backtest_auto(
    'm5_sample',
    item_id,
    ds,
    y,
    14,                         -- horizon: 14 days (2 weeks)
    3,                          -- folds: 3 CV folds
    '1d',                       -- frequency: daily
    MAP{
        'method': 'SeasonalNaive',
        'seasonal_period': '7'  -- weekly seasonality
    }
)
GROUP BY fold_id, model_name, fold_metric_score
ORDER BY fold_id;

-- ============================================================================
-- SECTION 3: Model Comparison
-- ============================================================================

SELECT
    '=== Model Comparison ===' AS section;

-- Compare multiple forecasting methods
CREATE OR REPLACE TABLE backtest_comparison AS
WITH naive_results AS (
    SELECT 'Naive' AS method, * FROM ts_backtest_auto(
        'm5_sample', item_id, ds, y, 14, 3, '1d',
        MAP{'method': 'Naive'}
    )
),
seasonal_naive_results AS (
    SELECT 'SeasonalNaive' AS method, * FROM ts_backtest_auto(
        'm5_sample', item_id, ds, y, 14, 3, '1d',
        MAP{'method': 'SeasonalNaive', 'seasonal_period': '7'}
    )
),
theta_results AS (
    SELECT 'Theta' AS method, * FROM ts_backtest_auto(
        'm5_sample', item_id, ds, y, 14, 3, '1d',
        MAP{'method': 'Theta', 'seasonal_period': '7'}
    )
)
SELECT * FROM naive_results
UNION ALL SELECT * FROM seasonal_naive_results
UNION ALL SELECT * FROM theta_results;

-- Summary by method
SELECT
    method,
    ROUND(AVG(abs_error), 2) AS avg_mae,
    ROUND(SQRT(AVG(error * error)), 2) AS avg_rmse,
    ROUND(AVG(error), 2) AS avg_bias,
    COUNT(*) AS n_predictions
FROM backtest_comparison
GROUP BY method
ORDER BY avg_mae;

-- Summary by method and fold (stability check)
SELECT
    method,
    fold_id,
    ROUND(AVG(abs_error), 2) AS mae,
    ROUND(SQRT(AVG(error * error)), 2) AS rmse
FROM backtest_comparison
GROUP BY method, fold_id
ORDER BY method, fold_id;

-- ============================================================================
-- SECTION 4: Backtest with Different Metrics
-- ============================================================================

SELECT
    '=== Different Metrics ===' AS section;

-- Test with SMAPE metric
SELECT
    'SMAPE Metric' AS metric_type,
    fold_id,
    ROUND(AVG(fold_metric_score), 2) AS smape
FROM ts_backtest_auto(
    'm5_sample', item_id, ds, y, 14, 3, '1d',
    MAP{'method': 'SeasonalNaive', 'seasonal_period': '7'},
    NULL,
    'smape'
)
GROUP BY fold_id
ORDER BY fold_id;

-- Test with Coverage metric (prediction interval coverage)
SELECT
    'Coverage Metric' AS metric_type,
    fold_id,
    ROUND(AVG(fold_metric_score), 2) AS coverage_90
FROM ts_backtest_auto(
    'm5_sample', item_id, ds, y, 14, 3, '1d',
    MAP{'method': 'SeasonalNaive', 'seasonal_period': '7'},
    NULL,
    'coverage'
)
GROUP BY fold_id
ORDER BY fold_id;

-- ============================================================================
-- SECTION 5: Backtest with Gap (Simulating ETL Latency)
-- ============================================================================

SELECT
    '=== Backtest with Gap ===' AS section;

-- Compare backtest with and without gap
SELECT
    'No Gap' AS scenario,
    ROUND(AVG(abs_error), 2) AS mae,
    ROUND(SQRT(AVG(error * error)), 2) AS rmse
FROM ts_backtest_auto(
    'm5_sample', item_id, ds, y, 14, 3, '1d',
    MAP{'method': 'SeasonalNaive', 'seasonal_period': '7', 'gap': '0'}
)
UNION ALL
SELECT
    'Gap=2 days' AS scenario,
    ROUND(AVG(abs_error), 2) AS mae,
    ROUND(SQRT(AVG(error * error)), 2) AS rmse
FROM ts_backtest_auto(
    'm5_sample', item_id, ds, y, 14, 3, '1d',
    MAP{'method': 'SeasonalNaive', 'seasonal_period': '7', 'gap': '2'}
);

-- ============================================================================
-- SECTION 6: Regression Backtest with OLS (requires anofox-statistics)
-- ============================================================================

SELECT
    '=== Regression Backtest with OLS ===' AS section;

-- First, create features for regression
-- We'll engineer: day_of_week, day_index (trend), is_weekend
CREATE OR REPLACE TABLE m5_with_features AS
SELECT
    item_id,
    ds,
    y,
    -- Feature: Day of week (0=Monday, 6=Sunday)
    EXTRACT(DOW FROM ds)::INTEGER AS day_of_week,
    -- Feature: Day index (trend) - days since start
    EXTRACT(EPOCH FROM (ds - '2011-01-29'::TIMESTAMP))::INTEGER / 86400 AS day_index,
    -- Feature: Is weekend
    CASE WHEN EXTRACT(DOW FROM ds) IN (0, 6) THEN 1 ELSE 0 END AS is_weekend,
    -- Feature: Month
    EXTRACT(MONTH FROM ds)::INTEGER AS month
FROM m5_sample;

-- Install and load the statistics extension
INSTALL anofox_statistics FROM community;
LOAD anofox_statistics;

-- Create CV splits for regression backtest
CREATE OR REPLACE TABLE cv_splits_reg AS
SELECT * FROM ts_cv_split(
    'm5_with_features',
    item_id,
    ds,
    y,
    ['2016-01-01', '2016-02-01', '2016-03-01']::DATE[],  -- 3 folds
    14,                                                    -- 14-day horizon
    '1d',
    MAP{}
);

-- Prepare regression input (masks target for test rows)
-- Add row numbers for joining back results
CREATE OR REPLACE TABLE reg_input AS
SELECT
    ROW_NUMBER() OVER (ORDER BY fold_id, group_col, date_col) AS row_num,
    *
FROM ts_prepare_regression_input(
    'cv_splits_reg',
    'm5_with_features',
    item_id,
    ds,
    y,
    MAP{}
);

-- Run OLS regression per fold
-- ols_fit_predict_by returns: group_id, y, x, yhat, yhat_lower, yhat_upper, is_training
-- Note: reg_input.y contains actual values (preserved for scoring), masked_target is NULL for test
CREATE OR REPLACE TABLE ols_backtest_results AS
WITH
-- Add row numbers to reg_input for joining with OLS results
reg_input_numbered AS (
    SELECT
        ROW_NUMBER() OVER (PARTITION BY fold_id ORDER BY group_col, date_col) AS row_in_fold,
        *
    FROM reg_input
),
-- Run OLS - results preserve input order within each group
ols_raw AS (
    SELECT
        ROW_NUMBER() OVER (PARTITION BY group_id ORDER BY group_id) AS row_in_fold,
        group_id AS fold_id,
        yhat AS forecast,
        is_training
    FROM ols_fit_predict_by(
        'reg_input',
        fold_id,
        masked_target,
        [day_of_week, day_index, is_weekend, month]
    )
),
-- Join OLS predictions with actual values from reg_input (y column has actuals)
joined AS (
    SELECT
        o.fold_id,
        r.group_col,
        r.date_col,
        o.forecast,
        r.y AS actual,
        r.split
    FROM ols_raw o
    JOIN reg_input_numbered r ON o.fold_id = r.fold_id AND o.row_in_fold = r.row_in_fold
)
SELECT
    fold_id,
    group_col,
    date_col,
    forecast,
    actual,
    forecast - actual AS error,
    ABS(forecast - actual) AS abs_error
FROM joined
WHERE split = 'test';

-- OLS Results Summary using built-in metric functions
SELECT
    'OLS Regression' AS model,
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(ts_mae(LIST(actual), LIST(forecast)), 2) AS mae,
    ROUND(ts_rmse(LIST(actual), LIST(forecast)), 2) AS rmse,
    ROUND(ts_bias(LIST(actual), LIST(forecast)), 2) AS bias
FROM ols_backtest_results
GROUP BY fold_id
ORDER BY fold_id;

-- Overall OLS vs SeasonalNaive comparison using built-in metrics
SELECT
    'OLS Regression' AS model,
    ROUND(ts_mae(LIST(actual), LIST(forecast)), 2) AS mae,
    ROUND(ts_rmse(LIST(actual), LIST(forecast)), 2) AS rmse,
    ROUND(ts_bias(LIST(actual), LIST(forecast)), 2) AS bias
FROM ols_backtest_results
UNION ALL
SELECT
    'SeasonalNaive' AS model,
    ROUND(ts_mae(LIST(actual), LIST(forecast)), 2) AS mae,
    ROUND(ts_rmse(LIST(actual), LIST(forecast)), 2) AS rmse,
    ROUND(ts_bias(LIST(actual), LIST(forecast)), 2) AS bias
FROM ts_backtest_auto(
    'm5_sample', item_id, ds, y, 14, 3, '1d',
    MAP{'method': 'SeasonalNaive', 'seasonal_period': '7'}
);

-- ============================================================================
-- SECTION 7: Per-Item Performance Analysis
-- ============================================================================

SELECT
    '=== Per-Item Performance ===' AS section;

-- Which items are hardest to forecast?
SELECT
    group_col AS item_id,
    ROUND(AVG(abs_error), 2) AS mae,
    ROUND(SQRT(AVG(error * error)), 2) AS rmse,
    ROUND(AVG(actual), 2) AS avg_actual,
    ROUND(AVG(abs_error) / NULLIF(AVG(actual), 0) * 100, 2) AS mape_pct
FROM ts_backtest_auto(
    'm5_sample', item_id, ds, y, 14, 3, '1d',
    MAP{'method': 'SeasonalNaive', 'seasonal_period': '7'}
)
GROUP BY group_col
ORDER BY mae DESC;

-- ============================================================================
-- CLEANUP
-- ============================================================================

SELECT
    '=== Backtesting Complete ===' AS section;

-- Optionally drop temporary tables
-- DROP TABLE IF EXISTS m5_full;
-- DROP TABLE IF EXISTS m5_sample;
-- DROP TABLE IF EXISTS m5_with_features;
-- DROP TABLE IF EXISTS backtest_comparison;
-- DROP TABLE IF EXISTS cv_splits_reg;
-- DROP TABLE IF EXISTS reg_input;
-- DROP TABLE IF EXISTS ols_backtest_results;
