-- ============================================================================
-- Synthetic Backtesting Examples
-- ============================================================================
-- This file demonstrates backtesting patterns using synthetic (generated) data.
-- Use this to learn the API before applying to your own datasets.
--
-- Patterns included:
--   1. Quick Start - One-liner with ts_backtest_auto
--   2. Regression with External Features - OLS with anofox-statistics
--   3. Production Reality - Using gap parameter for ETL latency
--   4. Composable Pipeline - Step-by-step modular approach
--   5. Unknown vs Known Features - Mask & Fill for feature leakage prevention
--
-- Prerequisites:
--   - anofox_forecast extension loaded
--   - anofox_statistics extension (for regression patterns)
-- ============================================================================

-- Load the extension
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- ============================================================================
-- PATTERN 1: Quick Start
-- ============================================================================
-- Scenario: Quick model evaluation with ts_backtest_auto one-liner

SELECT
    '=== Pattern 1: Quick Start ===' AS section;

-- Generate sample data: 3 stores, 90 days of daily sales
CREATE OR REPLACE TABLE sales_data AS
SELECT
    'Store_' || LPAD(s::VARCHAR, 2, '0') AS store_id,
    '2024-01-01'::DATE + (d * INTERVAL '1 day') AS date,
    ROUND(
        100.0 + s * 20.0                      -- Store baseline
        + 0.3 * d                              -- Trend
        + 15 * SIN(2 * PI() * d / 7)          -- Weekly seasonality
        + (RANDOM() * 10 - 5)                  -- Noise
    , 2)::DOUBLE AS revenue
FROM generate_series(0, 89) AS t(d)
CROSS JOIN generate_series(1, 3) AS s(s);

-- Backtest: Test AutoETS on 5 folds, 7-day horizon
SELECT
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(AVG(abs_error), 2) AS mae,
    ROUND(fold_metric_score, 2) AS rmse,
    model_name
FROM ts_backtest_auto(
    'sales_data',           -- source table
    store_id,               -- group column
    date,                   -- date column
    revenue,                -- target column
    7,                      -- horizon: forecast next 7 days
    5,                      -- folds: test on 5 different historical periods
    '1d',                   -- frequency
    MAP{'method': 'AutoETS'}, -- params: model selection
    NULL,                   -- features: no external factors
    'rmse'                  -- metric: RMSE for evaluation
)
GROUP BY fold_id, model_name, fold_metric_score
ORDER BY fold_id;

-- ============================================================================
-- PATTERN 2: Regression with External Features
-- ============================================================================
-- Scenario: Sales depend on temperature, holidays, promotions
-- Requires: anofox-statistics extension

SELECT
    '=== Pattern 2: Regression with External Features ===' AS section;

-- Generate sample data with external features
CREATE OR REPLACE TABLE sales_with_features AS
SELECT
    'Store_' || LPAD(s::VARCHAR, 2, '0') AS store_id,
    '2024-01-01'::DATE + (d * INTERVAL '1 day') AS date,
    -- Features
    ROUND(15.0 + 10 * SIN(2 * PI() * d / 365) + (RANDOM() * 5), 1)::DOUBLE AS temperature,
    CASE WHEN d % 7 IN (0, 6) THEN 1 ELSE 0 END AS is_holiday,
    CASE WHEN RANDOM() < 0.1 THEN 1 ELSE 0 END AS promotion_active,
    -- Target: revenue depends on features
    ROUND(
        100.0 + s * 20.0
        + 2.0 * (15.0 + 10 * SIN(2 * PI() * d / 365))  -- Temperature effect
        + 30.0 * CASE WHEN d % 7 IN (0, 6) THEN 1 ELSE 0 END  -- Holiday boost
        + 50.0 * CASE WHEN RANDOM() < 0.1 THEN 1 ELSE 0 END   -- Promotion boost
        + (RANDOM() * 10 - 5)
    , 2)::DOUBLE AS revenue
FROM generate_series(0, 89) AS t(d)
CROSS JOIN generate_series(1, 3) AS s(s);

-- Install and load statistics extension
INSTALL anofox_statistics FROM community;
LOAD anofox_statistics;

-- Step 1: Create CV splits
CREATE OR REPLACE TABLE cv_splits_p2 AS
SELECT * FROM ts_cv_split(
    'sales_with_features', store_id, date, revenue,
    ['2024-02-15', '2024-03-01']::DATE[],  -- 2 folds
    7, '1d', MAP{}
);

-- Step 2: Prepare regression input (masks target as NULL for test rows)
CREATE OR REPLACE TABLE reg_input_p2 AS
SELECT * FROM ts_prepare_regression_input(
    'cv_splits_p2', 'sales_with_features', store_id, date, revenue, MAP{}
);

-- Step 3: Run OLS fit-predict with multiple features
CREATE OR REPLACE TABLE ols_predictions_p2 AS
WITH
reg_input_numbered AS (
    SELECT
        ROW_NUMBER() OVER (PARTITION BY fold_id ORDER BY group_col, date_col) AS row_in_fold,
        *
    FROM reg_input_p2
),
ols_raw AS (
    SELECT
        ROW_NUMBER() OVER (PARTITION BY group_id ORDER BY group_id) AS row_in_fold,
        group_id AS fold_id,
        yhat AS forecast
    FROM ols_fit_predict_by(
        'reg_input_p2',
        fold_id,
        masked_target,
        [temperature, is_holiday, promotion_active]
    )
)
SELECT
    ri.fold_id,
    ri.group_col AS store_id,
    ri.date_col AS date,
    ols.forecast,
    ri.revenue AS actual
FROM ols_raw ols
JOIN reg_input_numbered ri ON ols.fold_id = ri.fold_id AND ols.row_in_fold = ri.row_in_fold
WHERE ri.split = 'test';

-- Calculate metrics using built-in functions
SELECT
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(ts_mae(LIST(actual), LIST(forecast)), 2) AS mae,
    ROUND(ts_rmse(LIST(actual), LIST(forecast)), 2) AS rmse,
    ROUND(ts_bias(LIST(actual), LIST(forecast)), 2) AS bias
FROM ols_predictions_p2
GROUP BY fold_id
ORDER BY fold_id;

-- ============================================================================
-- PATTERN 3: Production Reality (Gap Parameter)
-- ============================================================================
-- Scenario: ETL takes 2 days, so we can't use the last 2 days of data

SELECT
    '=== Pattern 3: Production Reality ===' AS section;

-- Reuse sales_data from Pattern 1
-- Backtest with gap=2 to simulate ETL latency
SELECT
    'With Gap=2' AS scenario,
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(AVG(abs_error), 2) AS mae
FROM ts_backtest_auto(
    'sales_data',
    store_id,
    date,
    revenue,
    7,                      -- horizon
    5,                      -- folds
    '1d',                   -- frequency
    MAP{
        'method': 'AutoARIMA',
        'gap': '2',         -- Skip 2 days between Train end and Test start
        'embargo': '0'      -- No embargo needed for point forecasts
    }
)
GROUP BY fold_id
ORDER BY fold_id;

-- Compare: Without gap (unrealistic but common mistake)
SELECT
    'Without Gap' AS scenario,
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(AVG(abs_error), 2) AS mae
FROM ts_backtest_auto(
    'sales_data',
    store_id,
    date,
    revenue,
    7, 5, '1d',
    MAP{'method': 'AutoARIMA', 'gap': '0'}
)
GROUP BY fold_id
ORDER BY fold_id;

-- ============================================================================
-- PATTERN 4: Composable Pipeline
-- ============================================================================
-- Scenario: Need total control for debugging or custom transformations

SELECT
    '=== Pattern 4: Composable Pipeline ===' AS section;

-- Step 1: Define fold boundaries (metadata only)
CREATE OR REPLACE TABLE fold_meta AS
SELECT * FROM ts_cv_generate_folds(
    'sales_data',           -- source table
    date,                   -- date column
    3,                      -- n_folds
    7,                      -- horizon
    '1d',                   -- frequency
    MAP{'gap': '1'}         -- params
);

SELECT 'Fold cutoff dates:' AS step;
SELECT * FROM fold_meta;

-- Step 2: Create CV splits
CREATE OR REPLACE TABLE cv_splits_p4 AS
SELECT * FROM ts_cv_split(
    'sales_data',
    store_id,
    date,
    revenue,
    (SELECT training_end_times FROM fold_meta),
    7,
    '1d',
    MAP{}
);

SELECT 'CV splits created:' AS step, COUNT(*) AS rows, COUNT(DISTINCT fold_id) AS folds FROM cv_splits_p4;

-- Step 3: Filter to training data only
CREATE OR REPLACE TABLE train_splits AS
SELECT * FROM cv_splits_p4 WHERE split = 'train';

-- Step 4: Run forecast on prepared data
SELECT
    fold_id,
    COUNT(*) AS n_forecasts,
    model_name
FROM ts_cv_forecast_by(
    'train_splits',
    group_col,
    date_col,
    target_col,
    'AutoETS',
    7,
    MAP{},
    '1d'
)
GROUP BY fold_id, model_name
ORDER BY fold_id;

-- ============================================================================
-- PATTERN 5: Unknown vs Known Features (Mask & Fill)
-- ============================================================================
-- Scenario: Prevent look-ahead bias by masking unknown features

SELECT
    '=== Pattern 5: Unknown vs Known Features ===' AS section;

-- Generate sample data with known and unknown features
CREATE OR REPLACE TABLE sales_features AS
SELECT
    'Store_' || LPAD(s::VARCHAR, 2, '0') AS store_id,
    '2024-01-01'::DATE + (d * INTERVAL '1 day') AS date,
    -- KNOWN feature: Calendar-based (known in advance)
    CASE WHEN (d % 7) IN (0, 6) THEN 1 ELSE 0 END AS is_holiday,
    -- UNKNOWN feature: Footfall (only known after the fact)
    ROUND(100 + 50 * CASE WHEN (d % 7) IN (0, 6) THEN 1 ELSE 0 END + RANDOM() * 30, 0)::INTEGER AS footfall,
    -- Target: Revenue depends on both features
    ROUND(
        50.0 + s * 10.0
        + 0.5 * (100 + 50 * CASE WHEN (d % 7) IN (0, 6) THEN 1 ELSE 0 END)
        + 20.0 * CASE WHEN (d % 7) IN (0, 6) THEN 1 ELSE 0 END
        + (RANDOM() * 10 - 5)
    , 2)::DOUBLE AS revenue
FROM generate_series(0, 89) AS t(d)
CROSS JOIN generate_series(1, 2) AS s(s);

-- Step 1: Create CV splits
CREATE OR REPLACE TABLE cv_splits_p5 AS
SELECT * FROM ts_cv_split(
    'sales_features', store_id, date, revenue,
    ['2024-02-15', '2024-03-01']::DATE[],
    7, '1d', MAP{}
);

-- Step 2: Hydrate features (join source data with CV splits)
CREATE OR REPLACE TABLE safe_data AS
SELECT * FROM ts_hydrate_features(
    'cv_splits_p5',
    'sales_features',
    store_id,
    date,
    MAP{}
);

-- Step 3: Manually mask unknown features in test rows
CREATE OR REPLACE TABLE masked_data AS
SELECT
    *,
    CASE WHEN _is_test THEN NULL ELSE footfall END AS footfall_safe
FROM safe_data;

-- Step 4: Fill unknowns using last known value
CREATE OR REPLACE TABLE filled_footfall AS
SELECT * FROM ts_fill_unknown(
    'masked_data',
    store_id,
    date,
    footfall_safe,
    (SELECT MAX(date) FROM masked_data WHERE split = 'train'),
    MAP{'strategy': 'last_value'}
);

-- Join filled values back to masked_data
CREATE OR REPLACE TABLE model_ready_data AS
SELECT
    m.fold_id,
    m.split,
    m.group_col,
    m.date_col,
    m.target_col,
    m.is_holiday,
    m.revenue,
    f.value_col AS footfall_filled
FROM masked_data m
JOIN filled_footfall f ON m.group_col = f.group_col AND m.date_col = f.date_col;

-- Step 5: Run OLS regression
-- (Requires anofox_statistics - already loaded from Pattern 2)

CREATE OR REPLACE TABLE reg_input_p5 AS
SELECT
    m.fold_id,
    m.split,
    m.group_col,
    m.date_col,
    m.revenue,
    m.is_holiday,
    m.footfall_filled,
    CASE WHEN m.split = 'test' THEN NULL ELSE m.revenue END AS masked_target
FROM model_ready_data m;

CREATE OR REPLACE TABLE ols_predictions_p5 AS
WITH
reg_input_numbered AS (
    SELECT
        ROW_NUMBER() OVER (PARTITION BY fold_id ORDER BY group_col, date_col) AS row_in_fold,
        *
    FROM reg_input_p5
),
ols_raw AS (
    SELECT
        ROW_NUMBER() OVER (PARTITION BY group_id ORDER BY group_id) AS row_in_fold,
        group_id AS fold_id,
        yhat AS forecast
    FROM ols_fit_predict_by(
        'reg_input_p5', fold_id, masked_target, [is_holiday, footfall_filled]
    )
)
SELECT
    ri.fold_id,
    ri.group_col AS store_id,
    ri.date_col AS date,
    ols.forecast,
    ri.revenue AS actual
FROM ols_raw ols
JOIN reg_input_numbered ri ON ols.fold_id = ri.fold_id AND ols.row_in_fold = ri.row_in_fold
WHERE ri.split = 'test';

-- Calculate metrics using built-in functions
SELECT
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(ts_mae(LIST(actual), LIST(forecast)), 2) AS mae,
    ROUND(ts_rmse(LIST(actual), LIST(forecast)), 2) AS rmse,
    ROUND(ts_bias(LIST(actual), LIST(forecast)), 2) AS bias
FROM ols_predictions_p5
GROUP BY fold_id
ORDER BY fold_id;

-- ============================================================================
-- CLEANUP
-- ============================================================================

SELECT
    '=== Examples Complete ===' AS section;

-- Optionally drop temporary tables
-- DROP TABLE IF EXISTS sales_data;
-- DROP TABLE IF EXISTS sales_with_features;
-- DROP TABLE IF EXISTS sales_features;
-- ... etc.
