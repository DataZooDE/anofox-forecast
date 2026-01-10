# Backtesting Examples Summary

## Quick Reference

| Example | Complexity | Key Concepts |
|---------|------------|--------------|
| [1. One-liner Basic](#1-one-liner-basic) | Simple | ts_backtest_auto basics |
| [2. One-liner with Options](#2-one-liner-with-options) | Simple | Method selection, gap, embargo |
| [3. Model Comparison](#3-model-comparison) | Intermediate | Multiple methods, aggregation |
| [4. Detailed Approach](#4-detailed-approach-modular) | Intermediate | ts_cv_split + ts_cv_forecast_by |
| [5. Regression Backtest](#5-regression-backtest) | Intermediate | External regressors, OLS |
| [6. Feature Handling](#6-feature-handling-known-vs-unknown) | Advanced | ts_hydrate_features, masking |
| [7. Complete Workflow](#7-complete-workflow) | Advanced | Full production pipeline |

---

## 1. One-liner Basic

**Simplest possible backtest - single function call:**

```sql
-- Complete backtest in ONE query
SELECT * FROM ts_backtest_auto(
    'sales_data',       -- Table name
    store_id,           -- Series identifier
    date,               -- Date column
    revenue,            -- Target column
    7,                  -- 7-day forecast horizon
    5,                  -- 5 CV folds
    '1d',               -- Daily frequency
    MAP{}               -- Default: AutoETS, expanding window
);
```

**Output:**
```
fold_id | group_col | date       | forecast | actual | error | abs_error | model_name
--------+-----------+------------+----------+--------+-------+-----------+-----------
      1 | Store_01  | 2024-02-01 |   125.3  | 128.0  |  -2.7 |       2.7 | Holt
      1 | Store_01  | 2024-02-02 |   127.1  | 124.5  |   2.6 |       2.6 | Holt
...
```

---

## 2. One-liner with Options

**Customizing method, window type, and leakage prevention:**

```sql
-- Naive method with gap and embargo
SELECT * FROM ts_backtest_auto(
    'sales_data', store_id, date, revenue, 7, 5, '1d',
    MAP{
        'method': 'Naive',          -- Naive forecast
        'gap': '2',                  -- 2-day data latency
        'embargo': '7',              -- Prevent label leakage
        'window_type': 'fixed',      -- Fixed training window
        'min_train_size': '30'       -- Minimum 30 days training
    }
);

-- With custom metric calculation
SELECT * FROM ts_backtest_auto(
    'sales_data', store_id, date, revenue, 7, 5, '1d',
    MAP{'method': 'Theta'},
    metric => 'mae'  -- fold_metric_score uses MAE
);
```

---

## 3. Model Comparison

**Compare multiple forecasting methods:**

```sql
-- Compare 5 methods across all folds
WITH methods AS (
    SELECT UNNEST(['Naive', 'SeasonalNaive', 'Theta', 'SES', 'AutoETS']) AS method
),
results AS (
    SELECT
        m.method,
        b.*
    FROM methods m
    CROSS JOIN LATERAL (
        SELECT * FROM ts_backtest_auto(
            'sales_data', store_id, date, revenue, 7, 3, '1d',
            MAP{'method': m.method}
        )
    ) b
)
SELECT
    method,
    ROUND(AVG(abs_error), 2) AS mae,
    ROUND(SQRT(AVG(error * error)), 2) AS rmse,
    COUNT(*) AS n_predictions
FROM results
GROUP BY method
ORDER BY mae;
```

**Output:**
```
method        | mae   | rmse  | n_predictions
--------------+-------+-------+--------------
SeasonalNaive | 12.34 | 15.67 | 42
Theta         | 13.21 | 16.89 | 42
AutoETS       | 14.56 | 18.23 | 42
SES           | 15.89 | 19.45 | 42
Naive         | 18.23 | 22.34 | 42
```

---

## 4. Detailed Approach (Modular)

**Step-by-step approach for maximum control:**

```sql
-- Step 1: Generate fold cutoff dates
CREATE TABLE fold_dates AS
SELECT training_end_times
FROM ts_cv_generate_folds('sales_data', date, 3, 7, '1d', MAP{});

-- Step 2: Create CV splits
CREATE TABLE cv_splits AS
SELECT * FROM ts_cv_split(
    'sales_data', store_id, date, revenue,
    (SELECT training_end_times FROM fold_dates),
    7, '1d', MAP{}
);

-- Step 3: Filter to training data
CREATE TABLE train_splits AS
SELECT * FROM cv_splits WHERE split = 'train';

-- Step 4: Generate forecasts (parallel across folds)
CREATE TABLE forecasts AS
SELECT * FROM ts_cv_forecast_by(
    'train_splits',
    group_col, date_col, target_col,
    'Theta', 7, MAP{}, '1d'
);

-- Step 5: Join forecasts with actuals
SELECT
    f.fold_id,
    f.id AS store_id,
    f.date,
    f.point_forecast AS forecast,
    t.target_col AS actual,
    f.point_forecast - t.target_col AS error,
    ABS(f.point_forecast - t.target_col) AS abs_error
FROM forecasts f
JOIN cv_splits t
    ON f.fold_id = t.fold_id
    AND f.id = t.group_col
    AND f.date = t.date_col
WHERE t.split = 'test'
ORDER BY f.fold_id, f.id, f.date;
```

**Note:** This produces identical results to `ts_backtest_auto` (verified by equivalence tests).

---

## 5. Regression Backtest

**Using regression models with external features (requires anofox-statistics):**

```sql
-- Create data with features
CREATE TABLE regression_data AS
SELECT
    series_id,
    date,
    revenue,
    day_index,  -- Feature: days since start
    temperature -- Feature: temperature
FROM raw_data;

-- Create CV splits
CREATE TABLE cv_splits AS
SELECT * FROM ts_cv_split(
    'regression_data', series_id, date, revenue,
    ['2024-03-01', '2024-04-01']::DATE[], 7, '1d', MAP{}
);

-- Install and load anofox-statistics extension
INSTALL anofox_statistics FROM community;
LOAD anofox_statistics;

-- Prepare regression input (masks target for test rows)
CREATE TABLE reg_input AS
SELECT * FROM ts_prepare_regression_input(
    'cv_splits', 'regression_data', series_id, date, revenue, MAP{}
);

-- Run OLS fit-predict per fold
SELECT
    group_id AS fold_id,
    ROUND(AVG(ABS(yhat - actual)), 2) AS mae
FROM ols_fit_predict_by(
    'reg_input', fold_id, masked_target, [day_index, temperature]
) ols
JOIN reg_input ri ON ols.row_id = ri.rowid
WHERE ri.split = 'test'
GROUP BY group_id;
```

---

## 6. Feature Handling (Known vs Unknown)

**Properly handling features that aren't known at forecast time:**

```sql
-- Create CV splits
CREATE TABLE cv_splits AS
SELECT * FROM ts_cv_split('sales_data', store_id, date, revenue,
    ['2024-03-01', '2024-04-01']::DATE[], 7, '1d', MAP{});

-- Hydrate features with masking
SELECT
    fold_id,
    split,
    group_col,
    date_col,
    target_col,

    -- KNOWN features: Use actual values (calendar, planned promotions)
    is_holiday,         -- Calendar is known in advance
    planned_promotion,  -- Promotions are planned ahead

    -- UNKNOWN features: Mask in test periods
    CASE WHEN _is_test THEN NULL ELSE temperature END AS temperature,
    CASE WHEN _is_test THEN NULL ELSE competitor_price END AS competitor_price

FROM ts_hydrate_features('cv_splits', 'sales_data', store_id, date, MAP{})
WHERE fold_id = 1;
```

**Feature Categories:**
| Feature Type | Strategy | Example |
|--------------|----------|---------|
| Known in advance | Use actual | `is_holiday`, `day_of_week` |
| Slowly changing | Forward fill | `temperature`, `price` |
| Event-driven | Default value | `promotion = 0` |
| Highly variable | NULL or forecast | `competitor_action` |

---

## 7. Complete Workflow

**Production-ready backtesting pipeline:**

```sql
-- ============================================
-- STEP 1: Create sample data (3 stores, 120 days)
-- ============================================
CREATE TABLE sales_data AS
SELECT
    'Store_' || LPAD(s::VARCHAR, 2, '0') AS store_id,
    '2024-01-01'::DATE + (d * INTERVAL '1 day') AS date,
    GREATEST(0,
        100.0 + s * 20.0                      -- Store baseline
        + 0.5 * d                              -- Trend
        + 20 * SIN(2 * PI() * d / 7)          -- Weekly seasonality
        + (RANDOM() * 10 - 5)                  -- Noise
    )::DOUBLE AS sales
FROM generate_series(0, 119) AS t(d)
CROSS JOIN generate_series(1, 3) AS s(s);

-- ============================================
-- STEP 2: One-liner backtest with multiple methods
-- ============================================
CREATE TABLE backtest_results AS
WITH methods AS (
    SELECT UNNEST(['Naive', 'SeasonalNaive', 'Theta', 'AutoETS']) AS method
)
SELECT
    m.method,
    b.fold_id,
    b.group_col AS store_id,
    b.date,
    b.forecast,
    b.actual,
    b.error,
    b.abs_error,
    b.fold_metric_score
FROM methods m
CROSS JOIN LATERAL (
    SELECT * FROM ts_backtest_auto(
        'sales_data', store_id, date, sales, 14, 3, '1d',
        MAP{'method': m.method},
        metric => 'rmse'
    )
) b;

-- ============================================
-- STEP 3: Summarize results
-- ============================================

-- Per-method overall performance
SELECT
    method,
    ROUND(AVG(abs_error), 2) AS mae,
    ROUND(SQRT(AVG(error * error)), 2) AS rmse,
    ROUND(AVG(ABS(error / NULLIF(actual, 0)) * 100), 2) AS mape_pct
FROM backtest_results
GROUP BY method
ORDER BY rmse;

-- Per-method, per-fold stability
SELECT
    method,
    fold_id,
    ROUND(AVG(abs_error), 2) AS mae,
    ROUND(SQRT(AVG(error * error)), 2) AS rmse
FROM backtest_results
GROUP BY method, fold_id
ORDER BY method, fold_id;

-- Per-store performance (identify problem stores)
SELECT
    method,
    store_id,
    ROUND(AVG(abs_error), 2) AS mae,
    COUNT(*) AS n_forecasts
FROM backtest_results
GROUP BY method, store_id
ORDER BY method, mae DESC;

-- ============================================
-- STEP 4: Best model selection
-- ============================================
WITH method_performance AS (
    SELECT
        method,
        AVG(abs_error) AS avg_mae,
        STDDEV(abs_error) AS std_mae,
        AVG(fold_metric_score) AS avg_rmse
    FROM backtest_results
    GROUP BY method
)
SELECT
    method,
    ROUND(avg_mae, 2) AS mae,
    ROUND(std_mae, 2) AS mae_std,
    ROUND(avg_rmse, 2) AS rmse,
    RANK() OVER (ORDER BY avg_mae) AS rank_mae,
    RANK() OVER (ORDER BY avg_rmse) AS rank_rmse
FROM method_performance
ORDER BY avg_mae;

-- Cleanup
DROP TABLE sales_data;
DROP TABLE backtest_results;
```

**Expected Output:**

```
-- Overall performance
method        | mae   | rmse  | mape_pct
--------------+-------+-------+----------
SeasonalNaive | 12.34 | 15.67 | 8.23
Theta         | 13.21 | 16.89 | 9.12
AutoETS       | 14.56 | 18.23 | 10.45
Naive         | 18.23 | 22.34 | 13.67

-- Best model selection
method        | mae   | mae_std | rmse  | rank_mae | rank_rmse
--------------+-------+---------+-------+----------+-----------
SeasonalNaive | 12.34 | 3.45    | 15.67 | 1        | 1
Theta         | 13.21 | 4.12    | 16.89 | 2        | 2
AutoETS       | 14.56 | 5.23    | 18.23 | 3        | 3
Naive         | 18.23 | 6.78    | 22.34 | 4        | 4
```

---

## Key Patterns

### Pattern 1: Quick Evaluation
```sql
SELECT * FROM ts_backtest_auto('data', id, date, value, 7, 5, '1d', MAP{});
```

### Pattern 2: Method Comparison
```sql
SELECT method, AVG(abs_error) FROM (
    SELECT 'Naive' AS method, * FROM ts_backtest_auto(..., MAP{'method': 'Naive'})
    UNION ALL
    SELECT 'Theta' AS method, * FROM ts_backtest_auto(..., MAP{'method': 'Theta'})
) GROUP BY method;
```

### Pattern 3: With Data Latency
```sql
SELECT * FROM ts_backtest_auto(..., MAP{'gap': '2'});
```

### Pattern 4: Preventing Label Leakage
```sql
SELECT * FROM ts_backtest_auto(..., MAP{'embargo': '7'});
```

### Pattern 5: Fixed Training Window
```sql
SELECT * FROM ts_backtest_auto(..., MAP{'window_type': 'fixed', 'min_train_size': '30'});
```

---

## Common Pitfalls

| Pitfall | Solution |
|---------|----------|
| Using future features in test | Use `ts_hydrate_features` with `_is_test` masking |
| Horizon mismatch | Match CV horizon to production horizon |
| Single fold evaluation | Use 3-5 folds for robust estimates |
| Ignoring data latency | Use `gap` parameter if ETL has delays |
| Forward-looking targets | Use `embargo` parameter |
