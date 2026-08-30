-- ============================================================================
-- Synthetic Backtesting Examples
-- ============================================================================
-- This file demonstrates backtesting patterns using synthetic (generated) data.
-- Use this to learn the API before applying to your own datasets.
--
-- Patterns included:
--   1. Quick Start - Two-step CV (ts_cv_folds_by + ts_cv_forecast_by)
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
-- Enable auto-load for json (required by ts_cv_hydrate_by, ts_cv_split_index_by, etc.)
SET autoinstall_known_extensions=1;
SET autoload_known_extensions=1;

-- ============================================================================
-- PATTERN 1: Quick Start
-- ============================================================================
-- Scenario: Quick model evaluation with the two-step CV workflow

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
-- Step 1: generate folds (both train and test rows)
CREATE OR REPLACE TABLE cv_folds_p1 AS
SELECT * FROM ts_cv_folds_by(
    'sales_data',           -- source table
    store_id,               -- group column
    date,                   -- date column
    revenue,                -- target column
    5,                      -- folds: test on 5 different historical periods
    7,                      -- horizon: forecast next 7 days
    MAP{}                   -- default fold parameters
);

-- Step 2: forecast on the folds (model chosen here; horizon inferred from test rows)
CREATE OR REPLACE TABLE cv_results_p1 AS
SELECT * FROM ts_cv_forecast_by(
    'cv_folds_p1', store_id, date, revenue, 'AutoETS', MAP{}
);

-- Step 3: metrics per fold (y = actual, yhat = forecast)
SELECT
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(ts_mae(LIST(y ORDER BY date), LIST(yhat ORDER BY date)), 2) AS mae,
    ROUND(ts_rmse(LIST(y ORDER BY date), LIST(yhat ORDER BY date)), 2) AS rmse,
    ANY_VALUE(model_name) AS model_name
FROM cv_results_p1
GROUP BY fold_id
ORDER BY fold_id;

-- ============================================================================
-- PATTERN 2: Regression with External Features
-- ============================================================================
-- Scenario: Sales depend on temperature, holidays, promotions
-- Requires: anofox-statistics extension
--
-- NOTE: ts_prepare_regression_input_by has been removed. This pattern now uses
-- ts_cv_folds_by + ts_cv_hydrate_by to mask unknown features, then
-- ols_fit_predict_by for regression. Known features (e.g. is_holiday) are joined
-- from the source table directly; unknown features go through ts_cv_hydrate_by
-- which fills test rows automatically using the specified fill strategy.

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

-- Step 1: Create CV folds (2 folds, 7-day horizon)
CREATE OR REPLACE TABLE cv_folds_p2 AS
SELECT * FROM ts_cv_folds_by(
    'sales_with_features', store_id, date, revenue,
    2,       -- 2 folds
    7,       -- 7-day horizon
    MAP{}
);

-- Step 2: Hydrate with unknown feature (temperature changes daily — mask test rows)
-- is_holiday is calendar-based (known in advance) so we join it directly below.
-- ts_cv_hydrate_by fills test-row temperature with the last training value per group/fold.
CREATE OR REPLACE TABLE cv_hydrated_p2 AS
SELECT * FROM ts_cv_hydrate_by(
    'cv_folds_p2',
    'sales_with_features',
    store_id,
    date,
    ['temperature'],               -- unknown feature: mask in test rows
    MAP{'strategy': 'last_value'}
);

-- Step 3: Join known feature (is_holiday) from source
CREATE OR REPLACE TABLE reg_input_p2 AS
SELECT
    h.fold_id,
    h.store_id,
    h.date,
    h.revenue,
    h.split,
    h.temperature,                -- masked in test rows by ts_cv_hydrate_by
    f.is_holiday,                 -- known: join directly from source
    f.promotion_active,           -- treat as known for this example
    CASE WHEN h.split = 'test' THEN NULL ELSE h.revenue END AS masked_target
FROM cv_hydrated_p2 h
JOIN sales_with_features f ON h.store_id = f.store_id AND h.date = f.date;

-- Step 4: Run OLS fit-predict with multiple features
CREATE OR REPLACE TABLE ols_predictions_p2 AS
WITH
reg_input_numbered AS (
    SELECT
        ROW_NUMBER() OVER (PARTITION BY fold_id ORDER BY store_id, date) AS row_in_fold,
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
    ri.store_id,
    ri.date,
    ols.forecast,
    ri.revenue AS actual
FROM ols_raw ols
JOIN reg_input_numbered ri ON ols.fold_id = ri.fold_id AND ols.row_in_fold = ri.row_in_fold
WHERE ri.split = 'test';

-- Calculate metrics using *_by table macros
WITH mae_results AS (
    SELECT * FROM ts_mae_by('ols_predictions_p2', fold_id, date, actual, forecast)
),
rmse_results AS (
    SELECT * FROM ts_rmse_by('ols_predictions_p2', fold_id, date, actual, forecast)
),
bias_results AS (
    SELECT * FROM ts_bias_by('ols_predictions_p2', fold_id, date, actual, forecast)
)
SELECT
    m.id AS fold_id,
    ROUND(m.mae, 2) AS mae,
    ROUND(r.rmse, 2) AS rmse,
    ROUND(b.bias, 2) AS bias
FROM mae_results m
JOIN rmse_results r ON m.id = r.id
JOIN bias_results b ON m.id = b.id
ORDER BY fold_id;

-- ============================================================================
-- PATTERN 3: Production Reality (Gap Parameter)
-- ============================================================================
-- Scenario: ETL takes 2 days, so we can't use the last 2 days of data

SELECT
    '=== Pattern 3: Production Reality ===' AS section;

-- Reuse sales_data from Pattern 1
-- The gap/embargo are fold-shape parameters, so they belong to ts_cv_folds_by.
-- Backtest with gap=2 to simulate ETL latency
CREATE OR REPLACE TABLE cv_folds_gap AS
SELECT * FROM ts_cv_folds_by(
    'sales_data', store_id, date, revenue,
    5,                      -- folds
    7,                      -- horizon
    MAP{
        'gap': '2',         -- Skip 2 days between Train end and Test start
        'embargo': '0'      -- No embargo needed for point forecasts
    }
);

SELECT
    'With Gap=2' AS scenario,
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(ts_mae(LIST(y ORDER BY date), LIST(yhat ORDER BY date)), 2) AS mae
FROM ts_cv_forecast_by('cv_folds_gap', store_id, date, revenue, 'AutoARIMA', MAP{})
GROUP BY fold_id
ORDER BY fold_id;

-- Compare: Without gap (unrealistic but common mistake)
CREATE OR REPLACE TABLE cv_folds_nogap AS
SELECT * FROM ts_cv_folds_by(
    'sales_data', store_id, date, revenue, 5, 7, MAP{'gap': '0'}
);

SELECT
    'Without Gap' AS scenario,
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(ts_mae(LIST(y ORDER BY date), LIST(yhat ORDER BY date)), 2) AS mae
FROM ts_cv_forecast_by('cv_folds_nogap', store_id, date, revenue, 'AutoARIMA', MAP{})
GROUP BY fold_id
ORDER BY fold_id;

-- ============================================================================
-- PATTERN 4: Composable Pipeline
-- ============================================================================
-- Scenario: Need total control for debugging or custom transformations

SELECT
    '=== Pattern 4: Composable Pipeline ===' AS section;

-- Step 1: Define fold boundaries (explicit dates for full control)
-- For automatic fold generation, use ts_cv_folds_by instead
CREATE OR REPLACE TABLE fold_meta AS
SELECT ['2024-01-22'::DATE, '2024-01-29'::DATE, '2024-02-05'::DATE] AS training_end_times;

SELECT 'Fold cutoff dates:' AS step;
SELECT * FROM fold_meta;

-- Step 2: Create CV splits (explicit cutoff dates; column names are preserved)
-- Pass the cutoff-date array as a literal (table macros accept at most one
-- subquery parameter, which is already the source table name).
CREATE OR REPLACE TABLE cv_splits_p4 AS
SELECT * FROM ts_cv_split_by(
    'sales_data',
    store_id,
    date,
    revenue,
    ['2024-01-22'::DATE, '2024-01-29'::DATE, '2024-02-05'::DATE],
    7,
    MAP{}
);

SELECT 'CV splits created:' AS step, COUNT(*) AS rows, COUNT(DISTINCT fold_id) AS folds FROM cv_splits_p4;

-- Step 3: Forecast on the splits.
-- ts_cv_forecast_by needs BOTH train and test rows (it trains on 'train',
-- predicts the 'test' dates), so pass the full splits table with the original
-- column names. Horizon is inferred from the test rows per fold.
SELECT
    fold_id,
    COUNT(*) AS n_forecasts,
    ANY_VALUE(model_name) AS model_name
FROM ts_cv_forecast_by(
    'cv_splits_p4',
    store_id,
    date,
    revenue,
    'AutoETS',
    MAP{}
)
GROUP BY fold_id
ORDER BY fold_id;

-- ============================================================================
-- PATTERN 5: Unknown vs Known Features (Mask & Fill)
-- ============================================================================
-- Scenario: Prevent look-ahead bias by masking unknown features
--
-- NOTE: ts_hydrate_features_by has been removed. This pattern now uses
-- ts_cv_hydrate_by, which automatically masks unknown features in test rows
-- (train rows receive actual values; test rows receive filled values).

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

-- Step 1: Create CV folds (2 folds, 7-day horizon)
CREATE OR REPLACE TABLE cv_folds_p5 AS
SELECT * FROM ts_cv_folds_by(
    'sales_features', store_id, date, revenue,
    2, 7, MAP{}
);

-- Step 2: Hydrate with UNKNOWN feature (footfall) — masking applied automatically
-- footfall column: train rows = actual, test rows = last training value per group/fold
CREATE OR REPLACE TABLE cv_hydrated_p5 AS
SELECT * FROM ts_cv_hydrate_by(
    'cv_folds_p5',
    'sales_features',
    store_id,
    date,
    ['footfall'],                  -- unknown: mask test rows
    MAP{'strategy': 'last_value'}
);

-- Step 3: Join KNOWN feature (is_holiday) directly — no masking needed
CREATE OR REPLACE TABLE reg_input_p5 AS
SELECT
    h.fold_id,
    h.store_id,
    h.date,
    h.revenue,
    h.split,
    f.is_holiday,            -- known: join directly (no masking)
    h.footfall::DOUBLE AS footfall_filled,  -- masked in test rows by ts_cv_hydrate_by
    CASE WHEN h.split = 'test' THEN NULL ELSE h.revenue END AS masked_target
FROM cv_hydrated_p5 h
JOIN sales_features f ON h.store_id = f.store_id AND h.date = f.date;

-- Step 4: Run OLS regression
-- (Requires anofox_statistics - already loaded from Pattern 2)
CREATE OR REPLACE TABLE ols_predictions_p5 AS
WITH
reg_input_numbered AS (
    SELECT
        ROW_NUMBER() OVER (PARTITION BY fold_id ORDER BY store_id, date) AS row_in_fold,
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
    ri.store_id,
    ri.date,
    ols.forecast,
    ri.revenue AS actual
FROM ols_raw ols
JOIN reg_input_numbered ri ON ols.fold_id = ri.fold_id AND ols.row_in_fold = ri.row_in_fold
WHERE ri.split = 'test';

-- Calculate metrics using *_by table macros
WITH mae_results AS (
    SELECT * FROM ts_mae_by('ols_predictions_p5', fold_id, date, actual, forecast)
),
rmse_results AS (
    SELECT * FROM ts_rmse_by('ols_predictions_p5', fold_id, date, actual, forecast)
),
bias_results AS (
    SELECT * FROM ts_bias_by('ols_predictions_p5', fold_id, date, actual, forecast)
)
SELECT
    m.id AS fold_id,
    ROUND(m.mae, 2) AS mae,
    ROUND(r.rmse, 2) AS rmse,
    ROUND(b.bias, 2) AS bias
FROM mae_results m
JOIN rmse_results r ON m.id = r.id
JOIN bias_results b ON m.id = b.id
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
CREATE OR REPLACE TABLE cv_folds_baseline AS
SELECT * FROM ts_cv_folds_by('scenario_baseline', store_id, date, revenue, 2, 14, MAP{});
SELECT
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(ts_mae(LIST(y ORDER BY date), LIST(yhat ORDER BY date)), 2) AS mae,
    ANY_VALUE(model_name) AS model_name
FROM ts_cv_forecast_by(
    'cv_folds_baseline', store_id, date, revenue,
    'SeasonalNaive', {'seasonal_period': '7'}
)
GROUP BY fold_id
ORDER BY fold_id;

SELECT 'What-if Scenario Results:' AS step;
CREATE OR REPLACE TABLE cv_folds_whatif AS
SELECT * FROM ts_cv_folds_by('scenario_whatif', store_id, date, revenue, 2, 14, MAP{});
SELECT
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(ts_mae(LIST(y ORDER BY date), LIST(yhat ORDER BY date)), 2) AS mae,
    ANY_VALUE(model_name) AS model_name
FROM ts_cv_forecast_by(
    'cv_folds_whatif', store_id, date, revenue,
    'SeasonalNaive', {'seasonal_period': '7'}
)
GROUP BY fold_id
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

-- Create larger sample data (5 stores × 100 days)
-- Use FLOOR() for integer-style grouping; direct i/100 gives floating-point in DuckDB.
CREATE OR REPLACE TABLE large_sales AS
SELECT
    'STORE' || (FLOOR((i - 1) / 100) + 1)::INTEGER::VARCHAR AS store_id,
    '2024-01-01'::DATE + INTERVAL ((i % 100)) DAY AS date,
    (100.0 + (i % 100) * 2 + RANDOM() * 20)::DOUBLE AS sales
FROM generate_series(1, 500) t(i);

.print '>>> Pattern 7: Memory-Efficient CV with ts_cv_split_index'
.print '-----------------------------------------------------------------------------'
.print 'Step 1: Create index-only CV splits (no data columns)'

-- ts_cv_split_index returns ONLY: group_col, date_col, fold_id, split
-- No target column = less memory for large datasets
CREATE OR REPLACE TABLE cv_index AS
SELECT * FROM ts_cv_split_index_by(
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
.print 'Step 2: Join back to source to get full data (ts_hydrate_split_full_by removed)'
.print '  Use a plain JOIN on group_col + date_col to retrieve all source columns'

-- Join index splits back to source table to get all data columns
-- Note: ts_hydrate_split_full_by has been removed; use a plain JOIN instead.
SELECT 'Hydrated data (joined from source):' AS info;
SELECT
    ci.fold_id, ci.split, ci.group_col AS store_id, ci.date_col AS date,
    ls.sales
FROM cv_index ci
JOIN large_sales ls ON ci.group_col = ls.store_id AND ci.date_col = ls.date
WHERE ci.group_col = 'STORE1'
ORDER BY ci.fold_id, ci.date_col
LIMIT 10;

.print ''
.print 'When to use:'
.print '  ts_cv_split_by       - Small/medium datasets, convenience'
.print '  ts_cv_split_index_by - Large datasets, memory efficiency'

-- ============================================================================
-- Pattern 8: Hydrate Functions
-- ============================================================================
-- Use case: Join CV folds with unknown features, preventing data leakage.
--
-- NOTE: ts_hydrate_split_by, ts_hydrate_split_full_by, and
-- ts_hydrate_split_strict_by have been removed. Use ts_cv_hydrate_by
-- (for folds created by ts_cv_folds_by) or a plain JOIN for index splits.

SELECT
    '=== Pattern 8: Hydrate Functions ===' AS section;

-- Create features table with known and unknown features
CREATE OR REPLACE TABLE store_features AS
SELECT
    store_id,
    date,
    EXTRACT(DOW FROM date)::INTEGER AS day_of_week,  -- KNOWN: calendar feature
    (RANDOM() * 100)::DOUBLE AS competitor_price     -- UNKNOWN: not available at forecast time
FROM large_sales;

.print '>>> Pattern 8: Hydrate Functions (ts_cv_hydrate_by)'
.print '-----------------------------------------------------------------------------'
.print 'ts_cv_hydrate_by works with folds from ts_cv_folds_by.'
.print 'For ts_cv_split_index_by output, use a plain JOIN to retrieve source columns.'

-- Create folds for this pattern (ts_cv_hydrate_by requires ts_cv_folds_by output)
CREATE OR REPLACE TABLE folds_p8 AS
SELECT * FROM ts_cv_folds_by('large_sales', store_id, date, sales, 2, 7, MAP{});

.print ''
.print 'ts_cv_hydrate_by: unknown feature auto-masked in test rows'
.print '  Train rows: actual competitor_price values'
.print '  Test rows:  last training value per group/fold (strategy: last_value)'

SELECT 'ts_cv_hydrate_by masks competitor_price in test rows:' AS info;
-- Note: ts_cv_hydrate_by returns unknown feature columns as VARCHAR;
-- cast to DOUBLE before arithmetic.
SELECT
    fold_id, split, store_id, date,
    day_of_week,                     -- join known feature separately below
    ROUND(competitor_price::DOUBLE, 2) AS competitor_price   -- masked in test rows
FROM ts_cv_hydrate_by(
    'folds_p8',
    'store_features',
    store_id,
    date,
    ['competitor_price'],            -- unknown: mask in test rows
    MAP{'strategy': 'last_value'}
)
WHERE store_id = 'STORE1' AND fold_id = 1
ORDER BY date
LIMIT 5;

.print ''
.print 'For KNOWN features (day_of_week), join directly from the source table:'

SELECT
    h.fold_id, h.split, h.store_id, h.date,
    sf.day_of_week,                           -- KNOWN: use directly from source
    ROUND(h.competitor_price::DOUBLE, 2) AS competitor_price  -- cast VARCHAR → DOUBLE
FROM ts_cv_hydrate_by(
    'folds_p8', 'store_features', store_id, date,
    ['competitor_price'], MAP{'strategy': 'last_value'}
) h
JOIN store_features sf ON h.store_id = sf.store_id AND h.date = sf.date
WHERE h.store_id = 'STORE1' AND h.fold_id = 1
ORDER BY h.date
LIMIT 5;

.print ''
.print 'Choosing an approach:'
.print '  ts_cv_hydrate_by                    - Unknown features, auto-masked (recommended)'
.print '  Plain JOIN on group + date          - Known features, or all columns from source'
.print '  ts_fill_unknown_by                  - Fill a single masked column after manual masking'

-- ============================================================================
-- Pattern 9: Data Leakage Audit
-- ============================================================================
-- Use case: Audit CV pipeline to ensure no data leakage.

SELECT
    '=== Pattern 9: Data Leakage Audit ===' AS section;

.print '>>> Pattern 9: Data Leakage Audit (ts_check_leakage)'
.print '-----------------------------------------------------------------------------'

-- Prepare data with masking applied via ts_cv_hydrate_by
-- Note: ts_hydrate_split_full_by has been removed; use ts_cv_hydrate_by instead.
-- ts_cv_hydrate_by requires folds_p8 (from ts_cv_folds_by); we use it here
-- since cv_index (from ts_cv_split_index_by) is a different format.
CREATE OR REPLACE TABLE cv_prepared AS
SELECT
    h.fold_id,
    h.split,
    h.store_id,
    h.date,
    (h.split = 'test') AS _is_test,
    sf.day_of_week,
    ROUND(h.competitor_price::DOUBLE, 2) AS competitor_price_masked  -- NULL in test rows; cast VARCHAR → DOUBLE
FROM ts_cv_hydrate_by(
    'folds_p8', 'store_features', store_id, date,
    ['competitor_price'], MAP{'strategy': 'null'}  -- NULL in test rows
) h
JOIN store_features sf ON h.store_id = sf.store_id AND h.date = sf.date;

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
DROP TABLE IF EXISTS folds_p8;
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
