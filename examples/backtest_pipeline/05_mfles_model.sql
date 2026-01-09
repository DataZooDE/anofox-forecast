-- =============================================================================
-- Apply MFLES Model and Forecasting with Exogenous Variables
-- =============================================================================
-- MFLES = Multiple Frequency Level Exponential Smoothing
--
-- This example demonstrates:
-- 1. Basic MFLES forecasting without exogenous variables
-- 2. ARIMAX forecasting with exogenous variables (promotion, month)
-- 3. Comparing forecasts with and without exog
-- 4. Backtesting with cross-validation splits
--
-- Note: Models supporting exogenous variables: ARIMA->ARIMAX, Theta->ThetaX
-- MFLES->MFLESX is available but currently has numerical issues
--
-- Prerequisites: Run 01_sample_data.sql first
-- =============================================================================

LOAD anofox_forecast;

-- =============================================================================
-- Step 1: Basic MFLES Forecast (No Exogenous Variables)
-- =============================================================================
-- Use ts_forecast_by for grouped forecasting

CREATE OR REPLACE TABLE fcst_mfles AS
SELECT
    id AS category,
    forecast_step,
    date,
    point_forecast,
    lower_90,
    upper_90,
    model_name
FROM ts_forecast_by(
    'backtest_sample',
    category,
    date,
    sales,
    'MFLES',      -- model
    3,            -- horizon (3 months)
    MAP{},        -- params
    frequency := '1mo'
);

SELECT * FROM fcst_mfles;

-- =============================================================================
-- Step 2: ARIMAX Forecast Using Scalar Function
-- =============================================================================
-- Use _ts_forecast_exog for forecasting with exogenous variables
-- This gives full control over the feature arrays
-- Using ARIMA which becomes ARIMAX when exogenous variables are provided

-- First, prepare the data by aggregating into arrays per category
CREATE OR REPLACE TABLE fcst_arimax AS
WITH prepared AS (
    SELECT
        category,
        MAX(date) AS last_date,
        LIST(sales ORDER BY date) AS y_values,
        -- Exogenous variables as separate lists
        LIST(promotion::DOUBLE ORDER BY date) AS x_promotion,
        LIST(SIN(2 * PI() * (EXTRACT(MONTH FROM date) - 1) / 12) ORDER BY date) AS x_month_sin,
        LIST(COS(2 * PI() * (EXTRACT(MONTH FROM date) - 1) / 12) ORDER BY date) AS x_month_cos
    FROM backtest_sample
    GROUP BY category
),
-- Generate future exogenous values (3 months: Jan, Feb, Mar 2025)
future_vals AS (
    SELECT
        -- Future promotion values (planned promotions in March)
        [0.0, 0.0, 1.0] AS future_promo,
        -- Future month_sin values for Jan, Feb, Mar
        [0.0, 0.5, 0.866] AS future_month_sin,
        -- Future month_cos values for Jan, Feb, Mar
        [1.0, 0.866, 0.5] AS future_month_cos
),
forecast_result AS (
    SELECT
        p.category,
        p.last_date,
        _ts_forecast_exog(
            p.y_values,
            [p.x_promotion, p.x_month_sin, p.x_month_cos],  -- historical X
            [f.future_promo, f.future_month_sin, f.future_month_cos],  -- future X
            3,  -- horizon
            'ARIMA'  -- becomes ARIMAX with exog
        ) AS fcst
    FROM prepared p, future_vals f
),
-- Unnest properly using generate_subscripts
unnested AS (
    SELECT
        category,
        last_date,
        generate_subscripts((fcst).point, 1) AS idx,
        (fcst).point AS points,
        (fcst).lower AS lowers,
        (fcst).upper AS uppers,
        (fcst).model AS model_name
    FROM forecast_result
)
SELECT
    category,
    idx AS forecast_step,
    (last_date + idx * INTERVAL '1 month')::TIMESTAMP AS date,
    points[idx] AS point_forecast,
    lowers[idx] AS lower_90,
    uppers[idx] AS upper_90,
    model_name
FROM unnested
ORDER BY category, idx;

SELECT * FROM fcst_arimax;

-- =============================================================================
-- Step 3: Compare MFLES vs ARIMAX Forecasts
-- =============================================================================

SELECT
    m.category,
    m.date,
    ROUND(m.point_forecast, 2) AS mfles_forecast,
    ROUND(x.point_forecast, 2) AS arimax_forecast,
    ROUND(x.point_forecast - m.point_forecast, 2) AS exog_effect,
    m.model_name AS mfles_model,
    x.model_name AS arimax_model
FROM fcst_mfles m
JOIN fcst_arimax x ON m.category = x.category AND m.forecast_step = x.forecast_step
ORDER BY m.category, m.date;

-- =============================================================================
-- Step 4: Backtesting with Cross-Validation
-- =============================================================================
-- Apply MFLESX to training data and evaluate on test set

-- Generate CV folds
CREATE OR REPLACE TABLE cv_fold_times AS
SELECT training_end_times
FROM ts_cv_generate_folds('backtest_sample', date, 3, 3, '1mo');

-- For demonstration, evaluate on Fold 1
-- Training end: July 2023, Test: Aug-Oct 2023
WITH fold1_config AS (
    SELECT (SELECT training_end_times FROM cv_fold_times)[1] AS train_end
),
-- Prepare training data
train_data AS (
    SELECT
        category,
        MAX(date) AS last_date,
        LIST(sales ORDER BY date) AS y_values,
        LIST(promotion::DOUBLE ORDER BY date) AS x_promotion,
        LIST(SIN(2 * PI() * (EXTRACT(MONTH FROM date) - 1) / 12) ORDER BY date) AS x_month_sin,
        LIST(COS(2 * PI() * (EXTRACT(MONTH FROM date) - 1) / 12) ORDER BY date) AS x_month_cos
    FROM backtest_sample
    WHERE date <= (SELECT train_end FROM fold1_config)
    GROUP BY category
),
-- Prepare future exog values from actual test data
test_exog AS (
    SELECT
        category,
        LIST(promotion::DOUBLE ORDER BY date) AS future_promo,
        LIST(SIN(2 * PI() * (EXTRACT(MONTH FROM date) - 1) / 12) ORDER BY date) AS future_month_sin,
        LIST(COS(2 * PI() * (EXTRACT(MONTH FROM date) - 1) / 12) ORDER BY date) AS future_month_cos
    FROM backtest_sample
    WHERE date > (SELECT train_end FROM fold1_config)
      AND date <= (SELECT train_end FROM fold1_config) + INTERVAL '3 months'
    GROUP BY category
),
-- Generate forecasts
fold1_fcst AS (
    SELECT
        t.category,
        t.last_date,
        _ts_forecast_exog(
            t.y_values,
            [t.x_promotion, t.x_month_sin, t.x_month_cos],
            [e.future_promo, e.future_month_sin, e.future_month_cos],
            3,
            'ARIMA'  -- becomes ARIMAX
        ) AS fcst
    FROM train_data t
    JOIN test_exog e ON t.category = e.category
),
-- Unnest forecasts properly
fold1_unnested AS (
    SELECT
        category,
        last_date,
        generate_subscripts((fcst).point, 1) AS idx,
        (fcst).point AS points,
        (fcst).model AS model_name
    FROM fold1_fcst
),
fold1_forecasts AS (
    SELECT
        category,
        (last_date + idx * INTERVAL '1 month')::TIMESTAMP AS date,
        points[idx] AS point_forecast,
        model_name
    FROM fold1_unnested
)
-- Compare to actuals
SELECT
    f.category,
    f.date::DATE AS forecast_date,
    ROUND(f.point_forecast, 2) AS forecast,
    a.sales AS actual,
    ROUND(f.point_forecast - a.sales, 2) AS error,
    ROUND(ABS(f.point_forecast - a.sales) / a.sales * 100, 1) AS mape_pct,
    f.model_name
FROM fold1_forecasts f
JOIN backtest_sample a ON f.category = a.category AND f.date = a.date
ORDER BY f.category, f.date;

-- =============================================================================
-- Step 5: Full Backtest Across All Folds
-- =============================================================================

CREATE OR REPLACE TABLE backtest_results AS
WITH fold_ends AS (
    SELECT
        1 AS fold_id,
        (SELECT training_end_times FROM cv_fold_times)[1] AS train_end
    UNION ALL
    SELECT 2, (SELECT training_end_times FROM cv_fold_times)[2]
    UNION ALL
    SELECT 3, (SELECT training_end_times FROM cv_fold_times)[3]
),
-- This would require dynamic execution which SQL doesn't support directly
-- For a complete backtest, iterate through folds programmatically
-- Here we show the structure for one fold
sample_backtest AS (
    SELECT
        1 AS fold_id,
        'Apparel' AS category,
        '2023-08-01'::DATE AS date,
        400.0 AS forecast,
        387.0 AS actual,
        'MFLESX' AS model
)
SELECT * FROM sample_backtest WHERE 1=0;  -- Empty placeholder

-- =============================================================================
-- Step 6: Summary Statistics
-- =============================================================================

SELECT
    'MFLES (no exog)' AS model_type,
    COUNT(*) AS n_forecasts,
    ROUND(AVG(point_forecast), 2) AS avg_forecast,
    ROUND(MIN(point_forecast), 2) AS min_forecast,
    ROUND(MAX(point_forecast), 2) AS max_forecast
FROM fcst_mfles
UNION ALL
SELECT
    'ARIMAX (with exog)',
    COUNT(*),
    ROUND(AVG(point_forecast), 2),
    ROUND(MIN(point_forecast), 2),
    ROUND(MAX(point_forecast), 2)
FROM fcst_arimax;

-- =============================================================================
-- Key Takeaways
-- =============================================================================
-- 1. MFLES is excellent for multiple seasonality without exogenous variables
-- 2. For exogenous variables, use ARIMA (becomes ARIMAX) or Theta (becomes ThetaX)
-- 3. Use _ts_forecast_exog scalar function for full control
-- 4. Exogenous variables must be provided as LIST of LISTs: [[x1], [x2], ...]
-- 5. Historical X length must match Y length
-- 6. Future X length must match forecast horizon
-- 7. Known features (promotion, calendar) work well as exogenous variables
