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
--   6. Scenario Calendar - What-if analysis with date-specific interventions
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
FROM ts_backtest_auto_by(
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
SELECT * FROM ts_cv_split_by(
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
FROM ts_backtest_auto_by(
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
FROM ts_backtest_auto_by(
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
SELECT * FROM ts_cv_split_by(
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
SELECT * FROM ts_cv_split_by(
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
-- PATTERN 6: Scenario Calendar (What-If Analysis)
-- ============================================================================
-- Scenario: Test hypothetical interventions on specific dates
-- Example: "What if we ran promotions on Feb 20-22 and Mar 5-7?"
--
-- Key concept: Create a date-based calendar to apply features only during
-- specific periods. This is useful for:
--   - Promotions that run on specific dates
--   - Planned price changes
--   - Marketing campaigns
--   - Seasonal events

SELECT
    '=== Pattern 6: Scenario Calendar ===' AS section;

-- Step 1: Create base data with HISTORICAL promotions (for OLS to learn the effect)
-- We add historical promotions to training period so OLS can estimate the coefficient
CREATE OR REPLACE TABLE sales_scenario AS
SELECT
    store_id,
    date,
    temperature,
    is_holiday,
    -- Historical promotions on specific past dates (in training period)
    CASE WHEN date IN ('2024-01-15'::DATE, '2024-01-16'::DATE, '2024-02-01'::DATE, '2024-02-02'::DATE)
         THEN 1 ELSE 0 END AS has_promo,
    -- Add promotion effect to revenue when promo is active
    revenue + CASE WHEN date IN ('2024-01-15'::DATE, '2024-01-16'::DATE, '2024-02-01'::DATE, '2024-02-02'::DATE)
                   THEN 30.0 ELSE 0 END AS revenue
FROM sales_with_features;

-- Step 2: Define scenario calendar - specific intervention periods
CREATE OR REPLACE TABLE promo_calendar AS
SELECT * FROM (VALUES
    ('2024-02-20'::DATE, '2024-02-22'::DATE, 'winter_sale'),
    ('2024-03-05'::DATE, '2024-03-07'::DATE, 'spring_launch')
) AS t(start_date, end_date, promo_name);

-- Show the calendar
SELECT 'Promo Calendar:' AS step;
SELECT * FROM promo_calendar;

-- Step 3: Create BASELINE scenario (historical promos in training, NO promos in test period)
CREATE OR REPLACE TABLE scenario_baseline AS
SELECT
    store_id,
    date,
    temperature,
    is_holiday,
    CASE
        WHEN date >= '2024-02-15'::DATE THEN 0  -- No promos in test period
        ELSE has_promo  -- Keep historical promos in training
    END AS has_promo,
    revenue
FROM sales_scenario;

-- Step 4: Create WHAT-IF scenario (historical promos + calendar promos in test period)
-- For test period (after Feb 15), apply promo calendar; otherwise keep historical
CREATE OR REPLACE TABLE scenario_whatif AS
SELECT
    s.store_id,
    s.date,
    s.temperature,
    s.is_holiday,
    CASE
        WHEN s.date >= '2024-02-15'::DATE AND p.promo_name IS NOT NULL THEN 1  -- Calendar promo
        ELSE s.has_promo  -- Historical promo
    END AS has_promo,
    s.revenue
FROM sales_scenario s
LEFT JOIN promo_calendar p
    ON s.date >= p.start_date AND s.date <= p.end_date;

-- Verify promotion counts
SELECT 'Baseline promo days:' AS step, SUM(has_promo) AS promo_days FROM scenario_baseline;
SELECT 'What-if promo days:' AS step, SUM(has_promo) AS promo_days FROM scenario_whatif;

-- Step 5: Run backtest on each scenario using SeasonalNaive
-- (SeasonalNaive ignores features, so forecast should be same - this shows structure)
SELECT 'Baseline Scenario Results:' AS step;
SELECT
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(AVG(abs_error), 2) AS mae,
    model_name
FROM ts_backtest_auto_by(
    'scenario_baseline', store_id, date, revenue,
    14, 2, '1d',
    MAP{'method': 'SeasonalNaive', 'seasonal_period': '7'}
)
GROUP BY fold_id, model_name
ORDER BY fold_id;

SELECT 'What-if Scenario Results:' AS step;
SELECT
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(AVG(abs_error), 2) AS mae,
    model_name
FROM ts_backtest_auto_by(
    'scenario_whatif', store_id, date, revenue,
    14, 2, '1d',
    MAP{'method': 'SeasonalNaive', 'seasonal_period': '7'}
)
GROUP BY fold_id, model_name
ORDER BY fold_id;

-- Step 6: For models that USE the promo feature, run OLS regression
-- This demonstrates the actual scenario comparison with feature impact

-- Prepare baseline for OLS (ts_cv_split + hydrate)
CREATE OR REPLACE TABLE cv_baseline AS
SELECT * FROM ts_cv_split_by(
    'scenario_baseline', store_id, date, revenue,
    ['2024-02-15']::DATE[], 14, '1d', MAP{}
);

CREATE OR REPLACE TABLE baseline_hydrated AS
SELECT
    c.*,
    s.temperature,
    s.is_holiday,
    s.has_promo
FROM cv_baseline c
JOIN scenario_baseline s ON c.group_col = s.store_id AND c.date_col = s.date;

-- Prepare what-if for OLS
CREATE OR REPLACE TABLE cv_whatif AS
SELECT * FROM ts_cv_split_by(
    'scenario_whatif', store_id, date, revenue,
    ['2024-02-15']::DATE[], 14, '1d', MAP{}
);

CREATE OR REPLACE TABLE whatif_hydrated AS
SELECT
    c.*,
    s.temperature,
    s.is_holiday,
    s.has_promo
FROM cv_whatif c
JOIN scenario_whatif s ON c.group_col = s.store_id AND c.date_col = s.date;

-- Run OLS on each scenario using ts_prepare_regression_input
CREATE OR REPLACE TABLE baseline_reg AS
SELECT
    h.fold_id,
    h.group_col,
    h.date_col,
    h.target_col,
    h.temperature,
    h.is_holiday,
    h.has_promo,
    h.split,
    CASE WHEN h.split = 'test' THEN NULL ELSE h.target_col END AS masked_target
FROM baseline_hydrated h;

CREATE OR REPLACE TABLE whatif_reg AS
SELECT
    h.fold_id,
    h.group_col,
    h.date_col,
    h.target_col,
    h.temperature,
    h.is_holiday,
    h.has_promo,
    h.split,
    CASE WHEN h.split = 'test' THEN NULL ELSE h.target_col END AS masked_target
FROM whatif_hydrated h;

-- Get OLS predictions for test rows only
-- Note: Using simpler approach - aggregate at fold level to avoid join issues
SELECT 'OLS Scenario Comparison:' AS step;
WITH
baseline_ols AS (
    SELECT group_id AS fold_id, yhat, is_training, y
    FROM ols_fit_predict_by('baseline_reg', fold_id, masked_target, [temperature, is_holiday, has_promo])
),
whatif_ols AS (
    SELECT group_id AS fold_id, yhat, is_training, y
    FROM ols_fit_predict_by('whatif_reg', fold_id, masked_target, [temperature, is_holiday, has_promo])
),
baseline_test AS (
    SELECT fold_id, SUM(yhat) AS total_forecast, COUNT(*) AS n
    FROM baseline_ols WHERE NOT is_training GROUP BY fold_id
),
whatif_test AS (
    SELECT fold_id, SUM(yhat) AS total_forecast, COUNT(*) AS n
    FROM whatif_ols WHERE NOT is_training GROUP BY fold_id
)
SELECT
    'baseline' AS scenario,
    b.fold_id,
    b.n AS n_predictions,
    ROUND(b.total_forecast, 2) AS total_forecast,
    (SELECT SUM(has_promo) FROM baseline_reg WHERE split = 'test') AS promo_days
FROM baseline_test b
UNION ALL
SELECT
    'with_promos' AS scenario,
    w.fold_id,
    w.n AS n_predictions,
    ROUND(w.total_forecast, 2) AS total_forecast,
    (SELECT SUM(has_promo) FROM whatif_reg WHERE split = 'test') AS promo_days
FROM whatif_test w
ORDER BY scenario;

-- Step 7: Estimate the promotion effect from OLS coefficients
-- Use ols_fit_agg aggregate to get model coefficients and extract has_promo coefficient
SELECT 'Promotion Effect Estimation:' AS step;

-- Train OLS on historical data to estimate the promotion coefficient
-- ols_fit_agg returns: {coefficients, intercept, r_squared, adj_r_squared, residual_std_error, n_observations, n_features}
-- Features order: [temperature, is_holiday, has_promo] → coefficients in same order
CREATE OR REPLACE TABLE ols_model AS
SELECT ols_fit_agg(target_col, [temperature, is_holiday, has_promo]) AS model
FROM baseline_reg
WHERE split = 'train';

-- Extract and display the promotion effect
SELECT
    'Promotion Effect' AS effect_name,
    ROUND(model.coefficients[3], 2) AS estimated_coefficient,  -- has_promo is 3rd coefficient
    30.0 AS true_effect,
    ROUND(ABS(model.coefficients[3] - 30.0), 2) AS estimation_error,
    ROUND(model.r_squared * 100, 1) AS r_squared_pct
FROM ols_model;

-- Show full model summary with all coefficients
SELECT 'OLS Model Coefficients:' AS step;
SELECT
    UNNEST(['intercept', 'temperature', 'is_holiday', 'has_promo']) AS feature,
    UNNEST([
        ROUND(model.intercept, 2),
        ROUND(model.coefficients[1], 2),
        ROUND(model.coefficients[2], 2),
        ROUND(model.coefficients[3], 2)
    ]) AS coefficient
FROM ols_model;

-- Calculate scenario impact using estimated effect
SELECT 'Scenario Impact Summary:' AS step;
SELECT
    (SELECT SUM(has_promo) FROM whatif_reg WHERE split = 'test') AS promo_days_in_scenario,
    ROUND(model.coefficients[3], 2) AS estimated_effect_per_day,
    ROUND((SELECT SUM(has_promo) FROM whatif_reg WHERE split = 'test') * model.coefficients[3], 2) AS expected_total_uplift,
    'Revenue uplift from running promotions on calendar dates' AS interpretation
FROM ols_model;

-- Step 8: Bootstrap confidence intervals for promotion effect
-- Resample training data 100 times to estimate uncertainty in the coefficient
SELECT 'Bootstrap Confidence Intervals (95%):' AS step;

-- First, prepare training data with row IDs
CREATE OR REPLACE TABLE training_for_bootstrap AS
SELECT ROW_NUMBER() OVER () AS row_id, target_col, temperature, is_holiday, has_promo
FROM baseline_reg WHERE split = 'train';

-- Create bootstrap samples by sampling row IDs with replacement
-- Each iteration samples n rows (with replacement) from the training data
CREATE OR REPLACE TABLE bootstrap_samples AS
WITH
n_info AS (SELECT COUNT(*) AS n FROM training_for_bootstrap),
-- Generate bootstrap iterations and random row selections
bootstrap_draws AS (
    SELECT
        iter,
        1 + FLOOR(random() * (SELECT n FROM n_info))::INTEGER AS sampled_row_id
    FROM
        (SELECT UNNEST(generate_series(1, 100)) AS iter) iters,
        (SELECT UNNEST(generate_series(1, (SELECT n FROM n_info))) AS draw_num) draws
),
-- Join back to get actual data for each sampled row
resampled AS (
    SELECT
        b.iter,
        t.target_col,
        t.temperature,
        t.is_holiday,
        t.has_promo
    FROM bootstrap_draws b
    JOIN training_for_bootstrap t ON b.sampled_row_id = t.row_id
)
-- Fit OLS on each bootstrap sample and extract has_promo coefficient
SELECT
    iter,
    (ols_fit_agg(target_col, [temperature, is_holiday, has_promo])).coefficients[3] AS promo_coef
FROM resampled
GROUP BY iter;

-- Calculate confidence interval from bootstrap distribution
SELECT
    ROUND(AVG(promo_coef), 2) AS mean_effect,
    ROUND(STDDEV(promo_coef), 2) AS std_error,
    ROUND(PERCENTILE_CONT(0.025) WITHIN GROUP (ORDER BY promo_coef), 2) AS ci_lower_95,
    ROUND(PERCENTILE_CONT(0.975) WITHIN GROUP (ORDER BY promo_coef), 2) AS ci_upper_95,
    ROUND(PERCENTILE_CONT(0.05) WITHIN GROUP (ORDER BY promo_coef), 2) AS ci_lower_90,
    ROUND(PERCENTILE_CONT(0.95) WITHIN GROUP (ORDER BY promo_coef), 2) AS ci_upper_90
FROM bootstrap_samples;

-- Show if true effect (30.0) is within confidence interval
SELECT 'Effect Significance:' AS step;
WITH ci AS (
    SELECT
        PERCENTILE_CONT(0.025) WITHIN GROUP (ORDER BY promo_coef) AS lower,
        PERCENTILE_CONT(0.975) WITHIN GROUP (ORDER BY promo_coef) AS upper,
        AVG(promo_coef) AS mean
    FROM bootstrap_samples
)
SELECT
    ROUND(mean, 2) AS estimated_effect,
    ROUND(lower, 2) AS ci_lower,
    ROUND(upper, 2) AS ci_upper,
    CASE
        WHEN lower > 0 THEN 'Significant positive effect (CI excludes zero)'
        WHEN upper < 0 THEN 'Significant negative effect (CI excludes zero)'
        ELSE 'Not significant (CI includes zero)'
    END AS significance,
    CASE
        WHEN 30.0 >= lower AND 30.0 <= upper THEN 'Yes - true effect within CI'
        ELSE 'No - true effect outside CI'
    END AS true_effect_in_ci
FROM ci;

-- ============================================================================
-- Pattern 7: Memory-Efficient CV with ts_cv_split_index
-- ============================================================================
-- Use case: Large datasets where duplicating data across folds is expensive.
-- Compare: ts_cv_split (returns full data) vs ts_cv_split_index (returns only index)

SELECT
    '=== Pattern 7: Memory-Efficient CV ===' AS section;

-- Create larger sample data
CREATE OR REPLACE TABLE large_sales AS
SELECT
    'STORE' || ((i / 100) + 1)::VARCHAR AS store_id,
    '2024-01-01'::DATE + INTERVAL ((i % 100)) DAY AS date,
    (100.0 + (i % 100) * 2 + RANDOM() * 20)::DOUBLE AS sales
FROM generate_series(1, 500) t(i);

.print '>>> Pattern 7: Memory-Efficient CV with ts_cv_split_index'
.print '-----------------------------------------------------------------------------'
.print 'Step 1: Create index-only CV splits (no data columns)'

-- ts_cv_split_index returns ONLY: group_col, date_col, fold_id, split
-- No target column = less memory for large datasets
CREATE OR REPLACE TABLE cv_index AS
SELECT * FROM ts_cv_split_index(
    'large_sales',
    store_id,
    date,
    ['2024-01-15'::DATE, '2024-01-22'::DATE],  -- 2 folds
    7,      -- 7-day horizon
    '1d',   -- daily frequency
    MAP{}
);

SELECT 'Index-only splits (note: no sales column):' AS info;
SELECT fold_id, split, COUNT(*) AS n_rows
FROM cv_index
GROUP BY fold_id, split
ORDER BY fold_id, split;

.print ''
.print 'Step 2: Hydrate with full data using ts_hydrate_split_full'

-- Join back to get all columns from source
SELECT 'Hydrated data (with all source columns):' AS info;
SELECT
    fold_id, split, store_id, date, sales,
    _is_test, _train_cutoff
FROM ts_hydrate_split_full_by(
    'cv_index', 'large_sales', store_id, date, MAP{}
)
WHERE store_id = 'STORE1'
ORDER BY fold_id, date
LIMIT 10;

.print ''
.print 'When to use:'
.print '  ts_cv_split       - Small/medium datasets, convenience'
.print '  ts_cv_split_index - Large datasets, memory efficiency'

-- ============================================================================
-- Pattern 8: Hydrate Functions Comparison
-- ============================================================================
-- Use case: Choose the right safety level for joining CV splits with features.
-- Compare: ts_hydrate_split vs ts_hydrate_split_full vs ts_hydrate_split_strict

SELECT
    '=== Pattern 8: Hydrate Functions Comparison ===' AS section;

-- Create features table with known and unknown features
CREATE OR REPLACE TABLE store_features AS
SELECT
    store_id,
    date,
    EXTRACT(DOW FROM date)::INTEGER AS day_of_week,  -- KNOWN: calendar feature
    (RANDOM() * 100)::DOUBLE AS competitor_price     -- UNKNOWN: not available at forecast time
FROM large_sales;

.print '>>> Pattern 8: Hydrate Functions Comparison'
.print '-----------------------------------------------------------------------------'

.print ''
.print 'Option A: ts_hydrate_split - Single column masking (auto)'
.print '  Use when: One unknown feature to mask'

SELECT 'ts_hydrate_split masks competitor_price in test set:' AS info;
SELECT
    fold_id, split, group_col AS store, date_col AS date,
    ROUND(unknown_col, 2) AS competitor_price
FROM ts_hydrate_split(
    'cv_index',
    'store_features',
    store_id,
    date,
    competitor_price,
    MAP{'strategy': 'null'}  -- mask to NULL in test
)
WHERE group_col = 'STORE1' AND fold_id = 1
ORDER BY date_col
LIMIT 5;

.print ''
.print 'Option B: ts_hydrate_split_full - All columns (manual mask)'
.print '  Use when: Multiple unknown features, need manual control'

SELECT 'ts_hydrate_split_full returns all columns with _is_test flag:' AS info;
SELECT
    fold_id, split, store_id, date,
    day_of_week,  -- KNOWN: use directly
    CASE WHEN _is_test THEN NULL ELSE ROUND(competitor_price, 2) END AS competitor_price  -- UNKNOWN: manual mask
FROM ts_hydrate_split_full_by(
    'cv_index', 'store_features', store_id, date, MAP{}
)
WHERE store_id = 'STORE1' AND fold_id = 1
ORDER BY date
LIMIT 5;

.print ''
.print 'Option C: ts_hydrate_split_strict - Metadata only (fail-safe)'
.print '  Use when: Production systems, audit requirements'

SELECT 'ts_hydrate_split_strict returns ONLY metadata:' AS info;
SELECT
    hs.fold_id, hs.split, hs.group_col AS store, hs.date_col AS date,
    hs._is_test
FROM ts_hydrate_split_strict(
    'cv_index', 'store_features', store_id, date, MAP{}
) hs
WHERE hs.group_col = 'STORE1' AND hs.fold_id = 1
ORDER BY hs.date_col
LIMIT 5;

.print ''
.print 'Choosing a Hydrate Function:'
.print '  ts_hydrate_split       - Single unknown feature, auto-masked'
.print '  ts_hydrate_split_full  - Multiple features, manual CASE masking'
.print '  ts_hydrate_split_strict - Fail-safe, explicit JOIN required'

-- ============================================================================
-- Pattern 9: Data Leakage Audit
-- ============================================================================
-- Use case: Audit CV pipeline to ensure no data leakage.

SELECT
    '=== Pattern 9: Data Leakage Audit ===' AS section;

.print '>>> Pattern 9: Data Leakage Audit (ts_check_leakage)'
.print '-----------------------------------------------------------------------------'

-- Prepare data with _is_test flag
CREATE OR REPLACE TABLE cv_prepared AS
SELECT
    fold_id, split, store_id, date,
    _is_test,
    day_of_week,
    CASE WHEN _is_test THEN NULL ELSE competitor_price END AS competitor_price_masked
FROM ts_hydrate_split_full_by(
    'cv_index', 'store_features', store_id, date, MAP{}
);

.print 'Audit prepared CV data:'
SELECT * FROM ts_check_leakage(
    'cv_prepared',
    _is_test,
    MAP{}
);

.print ''
.print 'When to use ts_check_leakage:'
.print '  - Before running expensive backtest'
.print '  - Audit production CV pipelines'
.print '  - Verify train/test separation'

-- Cleanup pattern 7-9 tables
DROP TABLE IF EXISTS large_sales;
DROP TABLE IF EXISTS cv_index;
DROP TABLE IF EXISTS store_features;
DROP TABLE IF EXISTS cv_prepared;

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
