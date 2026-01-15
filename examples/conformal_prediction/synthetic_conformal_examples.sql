-- =============================================================================
-- Conformal Prediction Examples - Table-Based Workflows
-- =============================================================================
-- This script demonstrates conformal prediction with the anofox-forecast
-- extension using realistic table-based workflows.
--
-- TRACK 1: TL;DR (30 seconds)       - Copy-paste, replace table names, done
-- TRACK 2: Complete Pipeline        - Full backtest -> conformalize workflow
-- TRACK 3: External Forecasts       - Add intervals to XGBoost/Prophet outputs
-- TRACK 4: Evaluation & Monitoring  - Measure and track interval quality
--
-- Run: ./build/release/duckdb < examples/conformal_prediction/synthetic_conformal_examples.sql
-- =============================================================================

LOAD anofox_forecast;

.print '============================================================================='
.print 'CONFORMAL PREDICTION EXAMPLES - Table-Based Workflows'
.print '============================================================================='

-- =============================================================================
-- TRACK 1: TL;DR (People in a Hurry)
-- =============================================================================
-- You have: backtest table with actuals and forecasts
-- You want: prediction intervals on new forecasts
-- Copy this, replace table names, done.

.print ''
.print '>>> TRACK 1: TL;DR - Add Prediction Intervals in 30 Seconds'
.print '-----------------------------------------------------------------------------'

-- Setup: Create sample backtest data (replace with your actual table)
CREATE OR REPLACE TEMP TABLE my_backtest AS
SELECT
    product_id,
    week,
    actual_sales,
    forecast_sales
FROM (
    VALUES
        ('SKU_A', 1, 120.0, 115.0), ('SKU_A', 2, 135.0, 130.0), ('SKU_A', 3, 128.0, 132.0),
        ('SKU_A', 4, 142.0, 138.0), ('SKU_A', 5, 155.0, 150.0), ('SKU_A', 6, 148.0, 152.0),
        ('SKU_A', 7, 160.0, 158.0), ('SKU_A', 8, 165.0, 162.0), ('SKU_A', 9, 158.0, 165.0),
        ('SKU_A', 10, 172.0, 168.0),
        ('SKU_B', 1, 80.0, 85.0), ('SKU_B', 2, 92.0, 88.0), ('SKU_B', 3, 88.0, 90.0),
        ('SKU_B', 4, 95.0, 92.0), ('SKU_B', 5, 102.0, 98.0), ('SKU_B', 6, 98.0, 100.0),
        ('SKU_B', 7, 105.0, 102.0), ('SKU_B', 8, 110.0, 108.0), ('SKU_B', 9, 108.0, 112.0),
        ('SKU_B', 10, 115.0, 110.0)
) AS t(product_id, week, actual_sales, forecast_sales);

-- Setup: Create forecasts to conformalize (replace with your forecast table)
CREATE OR REPLACE TEMP TABLE my_forecasts AS
SELECT
    product_id,
    week,
    point_forecast
FROM (
    VALUES
        ('SKU_A', 11, 175.0), ('SKU_A', 12, 180.0), ('SKU_A', 13, 185.0),
        ('SKU_B', 11, 118.0), ('SKU_B', 12, 122.0), ('SKU_B', 13, 125.0)
) AS t(product_id, week, point_forecast);

-- THE PATTERN: Add 90% prediction intervals per series
-- Step 1: Create calibration profiles (one per series)
-- Step 2: Apply to all forecasts at once (batch), then unnest

.print 'Forecasts with 90% prediction intervals (per product):'

-- Materialize calibration profiles
CREATE OR REPLACE TEMP TABLE my_profiles AS
SELECT
    p.product_id,
    ts_conformal_learn(
        (SELECT LIST(CAST(actual_sales - forecast_sales AS DOUBLE) ORDER BY week)
         FROM my_backtest b WHERE b.product_id = p.product_id),
        [0.1]::DOUBLE[],   -- 90% coverage (alpha=0.1)
        'symmetric',
        'split'
    ) AS profile
FROM (SELECT DISTINCT product_id FROM my_backtest) p;

-- Apply intervals to all forecasts at once (batch approach)
WITH batch_intervals AS (
    SELECT
        p.product_id,
        (SELECT LIST(week ORDER BY week) FROM my_forecasts f WHERE f.product_id = p.product_id) AS weeks,
        ts_conformal_apply(
            (SELECT LIST(point_forecast::DOUBLE ORDER BY week) FROM my_forecasts f WHERE f.product_id = p.product_id),
            p.profile
        ) AS iv
    FROM my_profiles p
)
SELECT
    product_id,
    unnest(weeks) AS week,
    ROUND(unnest((iv).point), 1) AS forecast,
    ROUND(unnest((iv).lower), 1) AS lower_90,
    ROUND(unnest((iv).upper), 1) AS upper_90
FROM batch_intervals
ORDER BY product_id, week;

DROP TABLE my_backtest;
DROP TABLE my_forecasts;
DROP TABLE my_profiles;

-- =============================================================================
-- TRACK 2: Complete Pipeline (Retail Sales with Backtest)
-- =============================================================================
-- Full workflow: Backtest data -> Compute residuals -> Conformalize -> Evaluate

.print ''
.print '>>> TRACK 2: Complete Pipeline - Retail Sales with Backtest'
.print '-----------------------------------------------------------------------------'

-- Step 1: Create realistic retail backtest data
-- In practice, this would be your historical forecasts vs actuals
.print ''
.print 'Step 1: Create backtest data (historical forecasts vs actuals)'

CREATE OR REPLACE TEMP TABLE retail_backtest AS
SELECT
    product_id,
    DATE '2024-01-01' + INTERVAL (week_num - 1) WEEK AS week_date,
    week_num,
    -- Simulated actual sales: base + trend + seasonality + noise
    ROUND(base_sales + week_num * trend + seasonal_amp * SIN(week_num * 0.12) + (RANDOM() - 0.5) * noise, 1) AS actual_sales,
    -- Simulated model forecasts (slightly off from actuals)
    ROUND(base_sales + week_num * trend + seasonal_amp * SIN(week_num * 0.12) + (RANDOM() - 0.5) * noise * 0.5, 1) AS forecast_sales
FROM (
    SELECT product_id, base_sales, trend, seasonal_amp, noise, week_num
    FROM (
        VALUES
            ('Premium_Widget', 500.0, 2.0, 50.0, 30.0),
            ('Basic_Gadget', 200.0, 0.5, 20.0, 15.0),
            ('Seasonal_Item', 300.0, 1.0, 80.0, 40.0)
    ) AS products(product_id, base_sales, trend, seasonal_amp, noise)
    CROSS JOIN generate_series(1, 52) AS weeks(week_num)
);

.print 'Backtest summary by product:'
SELECT
    product_id,
    COUNT(*) AS weeks,
    ROUND(AVG(actual_sales), 0) AS avg_actual,
    ROUND(AVG(forecast_sales), 0) AS avg_forecast,
    ROUND(AVG(actual_sales - forecast_sales), 2) AS mean_error,
    ROUND(STDDEV(actual_sales - forecast_sales), 2) AS std_error
FROM retail_backtest
GROUP BY product_id
ORDER BY product_id;

-- Step 2: Split into calibration (weeks 1-40) and holdout (weeks 41-52)
.print ''
.print 'Step 2: Split data for calibration and evaluation'

CREATE OR REPLACE TEMP TABLE calibration_data AS
SELECT * FROM retail_backtest WHERE week_num <= 40;

CREATE OR REPLACE TEMP TABLE holdout_data AS
SELECT * FROM retail_backtest WHERE week_num > 40;

-- Step 3: Learn calibration profiles per product
.print ''
.print 'Step 3: Learn calibration profiles (90% and 95% coverage)'

CREATE OR REPLACE TEMP TABLE calibration_profiles AS
SELECT
    p.product_id,
    ts_conformal_learn(
        (SELECT LIST(CAST(actual_sales - forecast_sales AS DOUBLE) ORDER BY week_num)
         FROM calibration_data c WHERE c.product_id = p.product_id),
        [0.1, 0.05]::DOUBLE[],  -- 90% and 95% coverage
        'symmetric',
        'split'
    ) AS profile
FROM (SELECT DISTINCT product_id FROM calibration_data) p;

.print 'Learned profiles:'
SELECT
    product_id,
    (profile).method,
    (profile).strategy,
    (profile).alphas,
    (profile).scores_lower AS conformity_scores,
    (profile).n_residuals
FROM calibration_profiles
ORDER BY product_id;

-- Step 4: Apply intervals to holdout forecasts and evaluate
.print ''
.print 'Step 4: Apply intervals to holdout and evaluate coverage'

WITH holdout_with_intervals AS (
    SELECT
        h.product_id,
        h.week_num,
        h.week_date,
        h.actual_sales,
        h.forecast_sales,
        ts_conformal_apply([h.forecast_sales]::DOUBLE[], cp.profile) AS iv
    FROM holdout_data h
    JOIN calibration_profiles cp ON h.product_id = cp.product_id
)
SELECT
    product_id,
    COUNT(*) AS n_periods,
    ROUND(100.0 * SUM(CASE WHEN actual_sales BETWEEN (iv).lower[1] AND (iv).upper[1] THEN 1 ELSE 0 END) / COUNT(*), 1) AS coverage_90_pct,
    ROUND(100.0 * SUM(CASE WHEN actual_sales BETWEEN (iv).lower[2] AND (iv).upper[2] THEN 1 ELSE 0 END) / COUNT(*), 1) AS coverage_95_pct,
    ROUND(AVG((iv).upper[1] - (iv).lower[1]), 2) AS avg_width_90,
    ROUND(AVG((iv).upper[2] - (iv).lower[2]), 2) AS avg_width_95
FROM holdout_with_intervals
GROUP BY product_id
ORDER BY product_id;

-- Step 5: Generate future forecasts with intervals
.print ''
.print 'Step 5: Future forecasts with prediction intervals'

CREATE OR REPLACE TEMP TABLE future_forecasts AS
SELECT
    product_id,
    DATE '2025-01-01' + INTERVAL (horizon - 1) WEEK AS forecast_date,
    horizon,
    -- Simulated future point forecasts
    ROUND(base + horizon * trend, 1) AS point_forecast
FROM (
    SELECT product_id, base, trend, horizon
    FROM (
        VALUES
            ('Premium_Widget', 600.0, 2.0),
            ('Basic_Gadget', 225.0, 0.5),
            ('Seasonal_Item', 350.0, 1.0)
    ) AS products(product_id, base, trend)
    CROSS JOIN generate_series(1, 4) AS horizons(horizon)
);

.print 'Final forecasts with prediction intervals:'
SELECT
    f.product_id,
    f.forecast_date,
    f.point_forecast AS forecast,
    ROUND((ts_conformal_apply([f.point_forecast]::DOUBLE[], cp.profile)).lower[1], 1) AS lower_90,
    ROUND((ts_conformal_apply([f.point_forecast]::DOUBLE[], cp.profile)).upper[1], 1) AS upper_90,
    ROUND((ts_conformal_apply([f.point_forecast]::DOUBLE[], cp.profile)).lower[2], 1) AS lower_95,
    ROUND((ts_conformal_apply([f.point_forecast]::DOUBLE[], cp.profile)).upper[2], 1) AS upper_95
FROM future_forecasts f
JOIN calibration_profiles cp ON f.product_id = cp.product_id
ORDER BY f.product_id, f.forecast_date;

-- Cleanup
DROP TABLE retail_backtest;
DROP TABLE calibration_data;
DROP TABLE holdout_data;
DROP TABLE calibration_profiles;
DROP TABLE future_forecasts;

-- =============================================================================
-- TRACK 3: External Forecasts (XGBoost/Prophet/Your Model)
-- =============================================================================
-- You have forecasts from an external model (XGBoost, Prophet, LightGBM, etc.)
-- You want to add prediction intervals using conformal prediction.

.print ''
.print '>>> TRACK 3: External Forecasts - Add Intervals to XGBoost/Prophet'
.print '-----------------------------------------------------------------------------'

-- Scenario: You ran XGBoost backtests and have results in a table
-- Table structure: series_id, date, actual, xgb_forecast

.print ''
.print 'Simulating XGBoost backtest results (your data would come from model)'

CREATE OR REPLACE TEMP TABLE xgb_backtest AS
SELECT
    series_id,
    DATE '2024-01-01' + INTERVAL (period - 1) DAY AS forecast_date,
    period,
    -- Simulated actuals
    ROUND(base + trend * period + seasonal * SIN(period * 0.2) + (RANDOM() - 0.5) * noise, 2) AS actual,
    -- Simulated XGBoost predictions (good but not perfect)
    ROUND(base + trend * period + seasonal * SIN(period * 0.2) + (RANDOM() - 0.5) * noise * 0.3, 2) AS xgb_forecast
FROM (
    SELECT series_id, base, trend, seasonal, noise, period
    FROM (
        VALUES
            ('store_1', 1000.0, 5.0, 200.0, 100.0),
            ('store_2', 500.0, 2.0, 80.0, 50.0),
            ('store_3', 2000.0, 10.0, 400.0, 200.0)
    ) AS series(series_id, base, trend, seasonal, noise)
    CROSS JOIN generate_series(1, 60) AS periods(period)  -- 60 days backtest
);

.print 'XGBoost backtest error summary by store:'
SELECT
    series_id,
    COUNT(*) AS n_days,
    ROUND(AVG(actual - xgb_forecast), 2) AS bias,
    ROUND(STDDEV(actual - xgb_forecast), 2) AS std_error,
    ROUND(AVG(ABS(actual - xgb_forecast)), 2) AS mae
FROM xgb_backtest
GROUP BY series_id
ORDER BY series_id;

-- Your new forecasts from XGBoost
CREATE OR REPLACE TEMP TABLE xgb_new_forecasts AS
SELECT
    series_id,
    DATE '2024-03-01' + INTERVAL (horizon - 1) DAY AS forecast_date,
    horizon,
    ROUND(base + trend * (60 + horizon) + seasonal * SIN((60 + horizon) * 0.2), 2) AS xgb_forecast
FROM (
    SELECT series_id, base, trend, seasonal, horizon
    FROM (
        VALUES
            ('store_1', 1000.0, 5.0, 200.0),
            ('store_2', 500.0, 2.0, 80.0),
            ('store_3', 2000.0, 10.0, 400.0)
    ) AS series(series_id, base, trend, seasonal)
    CROSS JOIN generate_series(1, 7) AS horizons(horizon)  -- 7 day forecast
);

-- THE PATTERN: Learn from backtest, apply to new forecasts
.print ''
.print 'Adding 90% prediction intervals to XGBoost forecasts:'

-- Materialize calibration profiles
CREATE OR REPLACE TEMP TABLE xgb_profiles AS
SELECT
    s.series_id,
    ts_conformal_learn(
        (SELECT LIST(CAST(actual - xgb_forecast AS DOUBLE) ORDER BY period)
         FROM xgb_backtest b WHERE b.series_id = s.series_id),
        [0.1]::DOUBLE[],
        'symmetric',
        'split'
    ) AS profile
FROM (SELECT DISTINCT series_id FROM xgb_backtest) s;

-- Apply intervals
SELECT
    f.series_id,
    f.forecast_date,
    f.horizon,
    f.xgb_forecast AS forecast,
    ROUND((ts_conformal_apply([f.xgb_forecast]::DOUBLE[], c.profile)).lower[1], 2) AS lower_90,
    ROUND((ts_conformal_apply([f.xgb_forecast]::DOUBLE[], c.profile)).upper[1], 2) AS upper_90,
    ROUND((ts_conformal_apply([f.xgb_forecast]::DOUBLE[], c.profile)).upper[1] -
          (ts_conformal_apply([f.xgb_forecast]::DOUBLE[], c.profile)).lower[1], 2) AS interval_width
FROM xgb_new_forecasts f
JOIN xgb_profiles c ON f.series_id = c.series_id
ORDER BY f.series_id, f.horizon;

DROP TABLE xgb_backtest;
DROP TABLE xgb_new_forecasts;
DROP TABLE xgb_profiles;

-- =============================================================================
-- TRACK 4: Evaluation & Monitoring
-- =============================================================================
-- Measure interval quality, detect when recalibration is needed

.print ''
.print '>>> TRACK 4: Evaluation & Monitoring'
.print '-----------------------------------------------------------------------------'

-- Create a scenario with known coverage for evaluation
CREATE OR REPLACE TEMP TABLE evaluation_data AS
SELECT
    series_id,
    period,
    actual,
    lower_bound,
    upper_bound,
    -- Check if actual is within bounds
    CASE WHEN actual >= lower_bound AND actual <= upper_bound THEN 1 ELSE 0 END AS covered
FROM (
    VALUES
        -- Series A: Good coverage (~90%)
        ('series_A', 1, 100.0, 95.0, 105.0),
        ('series_A', 2, 102.0, 97.0, 107.0),
        ('series_A', 3, 98.0, 93.0, 103.0),
        ('series_A', 4, 105.0, 98.0, 108.0),
        ('series_A', 5, 103.0, 99.0, 109.0),
        ('series_A', 6, 107.0, 101.0, 111.0),
        ('series_A', 7, 101.0, 96.0, 106.0),
        ('series_A', 8, 99.0, 94.0, 104.0),
        ('series_A', 9, 104.0, 98.0, 108.0),
        ('series_A', 10, 106.0, 100.0, 110.0),
        -- Series B: Poor coverage (~60%) - needs recalibration
        ('series_B', 1, 200.0, 195.0, 205.0),
        ('series_B', 2, 210.0, 196.0, 206.0),  -- miss
        ('series_B', 3, 195.0, 197.0, 207.0),  -- miss
        ('series_B', 4, 205.0, 198.0, 208.0),
        ('series_B', 5, 215.0, 199.0, 209.0),  -- miss
        ('series_B', 6, 203.0, 200.0, 210.0),
        ('series_B', 7, 220.0, 201.0, 211.0),  -- miss
        ('series_B', 8, 208.0, 202.0, 212.0),
        ('series_B', 9, 207.0, 203.0, 213.0),
        ('series_B', 10, 206.0, 204.0, 214.0)
) AS t(series_id, period, actual, lower_bound, upper_bound);

-- Use ts_conformal_coverage for quick coverage check
.print ''
.print 'Quick coverage check using ts_conformal_coverage:'
SELECT
    s.series_id,
    ROUND(ts_conformal_coverage(
        (SELECT LIST(CAST(actual AS DOUBLE) ORDER BY period) FROM evaluation_data e WHERE e.series_id = s.series_id),
        (SELECT LIST(CAST(lower_bound AS DOUBLE) ORDER BY period) FROM evaluation_data e WHERE e.series_id = s.series_id),
        (SELECT LIST(CAST(upper_bound AS DOUBLE) ORDER BY period) FROM evaluation_data e WHERE e.series_id = s.series_id)
    ) * 100, 1) AS coverage_pct,
    CASE
        WHEN ts_conformal_coverage(
            (SELECT LIST(CAST(actual AS DOUBLE) ORDER BY period) FROM evaluation_data e WHERE e.series_id = s.series_id),
            (SELECT LIST(CAST(lower_bound AS DOUBLE) ORDER BY period) FROM evaluation_data e WHERE e.series_id = s.series_id),
            (SELECT LIST(CAST(upper_bound AS DOUBLE) ORDER BY period) FROM evaluation_data e WHERE e.series_id = s.series_id)
        ) >= 0.85 THEN 'OK'
        ELSE 'NEEDS RECALIBRATION'
    END AS status
FROM (SELECT DISTINCT series_id FROM evaluation_data) s
ORDER BY s.series_id;

-- Use ts_conformal_evaluate for comprehensive metrics
.print ''
.print 'Comprehensive evaluation using ts_conformal_evaluate:'
SELECT
    s.series_id,
    (eval).coverage AS coverage,
    (eval).violation_rate AS violation_rate,
    ROUND((eval).mean_width, 2) AS mean_width,
    ROUND((eval).winkler_score, 2) AS winkler_score,
    (eval).n_observations AS n_obs
FROM (SELECT DISTINCT series_id FROM evaluation_data) s,
LATERAL (
    SELECT ts_conformal_evaluate(
        (SELECT LIST(CAST(actual AS DOUBLE) ORDER BY period) FROM evaluation_data e WHERE e.series_id = s.series_id),
        (SELECT LIST(CAST(lower_bound AS DOUBLE) ORDER BY period) FROM evaluation_data e WHERE e.series_id = s.series_id),
        (SELECT LIST(CAST(upper_bound AS DOUBLE) ORDER BY period) FROM evaluation_data e WHERE e.series_id = s.series_id),
        0.1  -- target alpha (90% coverage)
    ) AS eval
) t
ORDER BY s.series_id;

-- Manual coverage calculation (if you need custom logic)
.print ''
.print 'Manual coverage calculation:'
SELECT
    series_id,
    COUNT(*) AS n_observations,
    SUM(covered) AS n_covered,
    ROUND(100.0 * SUM(covered) / COUNT(*), 1) AS coverage_pct,
    ROUND(AVG(upper_bound - lower_bound), 2) AS avg_interval_width
FROM evaluation_data
GROUP BY series_id
ORDER BY series_id;

DROP TABLE evaluation_data;

-- =============================================================================
-- QUICK REFERENCE
-- =============================================================================
.print ''
.print '============================================================================='
.print 'QUICK REFERENCE'
.print '============================================================================='
.print ''
.print 'PATTERN: Learn calibration from backtest, apply to new forecasts'
.print ''
.print '  -- Step 1: Learn profile per series (using correlated subquery)'
.print '  WITH calibrated AS ('
.print '      SELECT'
.print '          p.series_id,'
.print '          ts_conformal_learn('
.print '              (SELECT LIST(CAST(actual - forecast AS DOUBLE) ORDER BY date)'
.print '               FROM backtest_table b WHERE b.series_id = p.series_id),'
.print '              [0.1]::DOUBLE[],    -- 90% coverage'
.print '              ''symmetric'','
.print '              ''split'''
.print '          ) AS profile'
.print '      FROM (SELECT DISTINCT series_id FROM backtest_table) p'
.print '  )'
.print ''
.print '  -- Step 2: Apply profile to new forecasts'
.print '  SELECT f.series_id, f.date, f.forecast,'
.print '         (ts_conformal_apply([f.forecast]::DOUBLE[], c.profile)).lower[1] AS lower,'
.print '         (ts_conformal_apply([f.forecast]::DOUBLE[], c.profile)).upper[1] AS upper'
.print '  FROM forecast_table f'
.print '  JOIN calibrated c ON f.series_id = c.series_id;'
.print ''
.print 'FUNCTIONS:'
.print '  ts_conformal_learn(residuals[], alphas[], method, strategy) -> Profile'
.print '  ts_conformal_apply(forecasts[], profile) -> Intervals'
.print '  ts_conformal_coverage(actuals[], lowers[], uppers[]) -> DOUBLE'
.print '  ts_conformal_evaluate(actuals[], lowers[], uppers[], alpha) -> Metrics'
.print ''
.print 'METHODS: symmetric | asymmetric | adaptive'
.print 'STRATEGIES: split | crossval | jackknife_plus'
.print ''
.print 'NOTE: Use correlated subquery pattern for multi-series.'
.print '      Always CAST to DOUBLE when aggregating with LIST().'
.print ''
.print '============================================================================='
.print 'EXAMPLES COMPLETE'
.print '============================================================================='
