# Backtesting Examples

> **Backtesting is the art of simulating the past to predict the future.**
>
> The anofox-forecast extension provides two paths: a high-level **"One-Liner"** for quick validation, and a **"Composable Pipeline"** for complex, real-world scenarios.

---

## Understanding Gap vs Embargo

Before diving into examples, understand these critical parameters that prevent data leakage:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         GAP vs EMBARGO Explained                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  GAP: Simulates data latency (ETL delays)                                   │
│  ─────────────────────────────────────────                                  │
│                                                                             │
│    Day:  1   2   3   4   5   6   7   8   9  10  11  12  13  14              │
│         [TRAIN TRAIN TRAIN TRAIN]     [TEST TEST TEST TEST]                 │
│                               │   ▲   │                                     │
│                               │   │   │                                     │
│                               └───┼───┘                                     │
│                                   │                                         │
│                              gap = 2 days                                   │
│                                                                             │
│    Reality: "I don't get Monday's data until Wednesday"                     │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  EMBARGO: Prevents label leakage between folds                              │
│  ─────────────────────────────────────────────                              │
│                                                                             │
│    Fold 1: [TRAIN TRAIN TRAIN][TEST TEST TEST]                              │
│    Fold 2:          ███████████[TRAIN TRAIN][TEST TEST TEST]                │
│                     ▲         ▲                                             │
│                     │         │                                             │
│                     └─────────┘                                             │
│                     embargo = 3 days                                        │
│                     (excluded from fold 2 training)                         │
│                                                                             │
│    Reality: "Rolling 7-day sales target overlaps previous test window"      │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Pattern 1: The Quick Start

### The Scenario

You want to quickly check if AutoETS works on your sales data. No external factors, just pure time-series forecasting.

### The SQL Code

```sql
-- The fastest way to evaluate a model
-- Test AutoETS on the last 5 weeks of data, forecasting 7 days ahead each time
SELECT * FROM ts_backtest_auto(
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
);
```

### Why It Works: Key Parameters

| Parameter | Value | Explanation |
|-----------|-------|-------------|
| `horizon = 7` | 7 days | Matches your production forecast window |
| `folds = 5` | 5 folds | Tests across 5 different time periods for robust evaluation |
| `features = NULL` | No features | **The model only looks at its own history** - pure univariate forecasting |
| `metric = 'rmse'` | RMSE | Returns `fold_metric_score` column with RMSE per fold |

> **Key Insight:** When `features = NULL`, the model uses only the target column's historical values. This is ideal for series with strong autocorrelation but no known external drivers.

---

## Pattern 2: Regression with External Features

### The Scenario

Your sales depend on temperature, holidays, and promotions. Standard ARIMA might fail because it ignores these factors. You need a regression model.

### Important: Regression Models Require the anofox-statistics Extension

Unlike univariate models (AutoETS, ARIMA, Theta), regression models require the **anofox-statistics** extension and explicit data preparation:

```sql
-- Install the statistics extension (run once)
INSTALL anofox_statistics FROM community;
LOAD anofox_statistics;

-- 1. Create CV splits
CREATE TEMP TABLE cv_splits AS
SELECT * FROM ts_cv_split(
    'sales_data', store_id, date, revenue,
    ['2024-03-01', '2024-04-01']::DATE[],
    7, '1d', MAP{}
);

-- 2. Prepare regression input (masks target as NULL for test rows)
CREATE TEMP TABLE reg_input AS
SELECT * FROM ts_prepare_regression_input(
    'cv_splits', 'sales_data', store_id, date, revenue, MAP{}
);

-- 3. Run OLS fit-predict with multiple features
SELECT
    ri.fold_id,
    ri.group_col AS store_id,
    ri.date_col AS date,
    ols.yhat AS forecast,
    ri.actual_target AS actual,
    ols.yhat - ri.actual_target AS error,
    ABS(ols.yhat - ri.actual_target) AS abs_error
FROM ols_fit_predict_by(
    'reg_input',
    fold_id,
    masked_target,          -- NULL for test rows, actual for train
    [temperature, is_holiday, promotion_active]  -- multiple features
) ols
JOIN reg_input ri ON ols.row_id = ri.rowid
WHERE ri.split = 'test'
ORDER BY ri.fold_id, ri.group_col, ri.date_col;
```

### Why It Works: Key Concepts

| Concept | Explanation |
|---------|-------------|
| `ts_prepare_regression_input` | Masks target as NULL for test rows, enabling fit-predict in one pass |
| `ols_fit_predict_by` | From anofox-statistics - fits on non-NULL targets, predicts for NULL targets |
| Multiple features | anofox-statistics supports any number of feature columns |

> **Key Insight:** Regression backtesting requires the **modular approach** because the model needs to see features at both train and test time. The `masked_target = NULL` pattern tells the model "fit on these rows (target known), predict on those rows (target unknown)."

---

## Pattern 3: Production Reality

### The Scenario

In the real world, you rarely have today's data available immediately. Your ETL might take 2 days. Your target might be a rolling 7-day average that overlaps test windows.

### The SQL Code

```sql
-- Simulate real-world data latency and prevent label leakage
SELECT * FROM ts_backtest_auto(
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
);
```

### Why It Works: Key Parameters

| Parameter | Value | Explanation |
|-----------|-------|-------------|
| `gap: '2'` | 2 days | **Simulates ETL latency** - training data ends 2 days before test begins |
| `embargo: '0'` | No embargo | Point forecasts don't overlap, so no label leakage risk |

> **Warning: Ignoring data latency is the #1 cause of failed production deployments.**
>
> If your ETL takes 24 hours, set `gap: '1'`. If it takes 48 hours, set `gap: '2'`.
>
> A backtest without the gap will show artificially high accuracy that won't replicate in production.

---

## Pattern 4: The Composable Pipeline

### The Scenario

You need total control: debugging specific folds, custom feature engineering, or examining intermediate outputs. Break the process into discrete steps.

### The SQL Code

```sql
-- Step 1: Define the fold boundaries (Metadata only)
-- This generates cutoff dates without touching your data
CREATE TEMP TABLE fold_meta AS
SELECT * FROM ts_cv_generate_folds(
    'sales_data',           -- source table
    date,                   -- date column
    3,                      -- n_folds
    7,                      -- horizon
    '1d',                   -- frequency
    MAP{'gap': '1'}         -- params
);

-- Step 2: Create the CV splits
-- Assigns each row to folds with train/test labels
CREATE TEMP TABLE cv_splits AS
SELECT * FROM ts_cv_split(
    'sales_data',
    store_id,               -- group column
    date,                   -- date column
    revenue,                -- target column
    (SELECT training_end_times FROM fold_meta),  -- fold dates from step 1
    7,                      -- horizon
    '1d',                   -- frequency
    MAP{}                   -- params
);

-- Step 3: Filter to training data only
CREATE TEMP TABLE train_splits AS
SELECT * FROM cv_splits WHERE split = 'train';

-- Step 4: Run the forecast on the prepared data
SELECT * FROM ts_cv_forecast_by(
    'train_splits',
    group_col,              -- group column (from cv_splits output)
    date_col,               -- date column (from cv_splits output)
    target_col,             -- target column (from cv_splits output)
    'AutoETS',              -- method
    7,                      -- horizon
    MAP{},                  -- params
    '1d'                    -- frequency
);
```

### Why It Works: Key Steps

| Step | Function | Purpose |
|------|----------|---------|
| 1 | `ts_cv_generate_folds` | Generate fold cutoff dates only (no data movement) |
| 2 | `ts_cv_split` | Create train/test splits with fold assignments |
| 3 | Filter | Extract training data |
| 4 | `ts_cv_forecast_by` | Run forecasts in parallel across all folds |

> **Key Insight:** This produces **identical results** to `ts_backtest_auto` but lets you inspect and modify each stage. Use this when debugging or when you need custom transformations between steps.

---

## Pattern 5: Unknown vs Known Features (Mask & Fill)

### The Scenario

To prevent "Look-ahead Bias," you must distinguish between:
- **Known features**: Values you know in advance (e.g., holidays, planned promotions)
- **Unknown features**: Values you won't have at forecast time (e.g., actual customer footfall)

### Understanding Hydration

> **Definition:** *Hydration* is the process of joining features back to the time-series spine. *Safe Hydration* automatically hides future data to prevent leakage.

### The Mask & Fill Process

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         MASK & FILL Process                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  BEFORE MASKING (Raw Data)                                                  │
│  ─────────────────────────                                                  │
│                                                                             │
│    Date       │ Split │ is_holiday │ footfall │ revenue                     │
│    ───────────┼───────┼────────────┼──────────┼─────────                    │
│    2024-01-01 │ train │     0      │   150    │  1200                       │
│    2024-01-02 │ train │     0      │   145    │  1150                       │
│    2024-01-03 │ train │     1      │   200    │  1800   ← Last train day    │
│    2024-01-04 │ test  │     0      │   160    │  1300   ← DANGER: Future!   │
│    2024-01-05 │ test  │     0      │   155    │  1250                       │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  AFTER MASKING (ts_hydrate_features with unknown_features = ['footfall'])  │
│  ─────────────────────────────────────────────────────────────────────────  │
│                                                                             │
│    Date       │ Split │ is_holiday │ footfall │ revenue                     │
│    ───────────┼───────┼────────────┼──────────┼─────────                    │
│    2024-01-01 │ train │     0      │   150    │  1200                       │
│    2024-01-02 │ train │     0      │   145    │  1150                       │
│    2024-01-03 │ train │     1      │   200    │  1800                       │
│    2024-01-04 │ test  │     0      │   NULL   │  1300   ← footfall masked   │
│    2024-01-05 │ test  │     0      │   NULL   │  1250   ← footfall masked   │
│                                                                             │
│    Note: is_holiday passes through (it's a KNOWN feature - calendar data)  │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  AFTER FILLING (ts_fill_unknown with method = 'last_value')                │
│  ─────────────────────────────────────────────────────────────              │
│                                                                             │
│    Date       │ Split │ is_holiday │ footfall │ revenue                     │
│    ───────────┼───────┼────────────┼──────────┼─────────                    │
│    2024-01-01 │ train │     0      │   150    │  1200                       │
│    2024-01-02 │ train │     0      │   145    │  1150                       │
│    2024-01-03 │ train │     1      │   200    │  1800                       │
│    2024-01-04 │ test  │     0      │   200    │  1300   ← Filled from train │
│    2024-01-05 │ test  │     0      │   200    │  1250   ← Filled from train │
│                                                                             │
│    The model now uses "Planned Holiday" + "Estimated Footfall"             │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### The SQL Code

```sql
-- 1. Create the CV splits
CREATE TEMP TABLE cv_splits AS
SELECT * FROM ts_cv_split(
    'sales_data', store_id, date, revenue,
    ['2024-03-01', '2024-04-01']::DATE[],  -- training end times
    7, '1d', MAP{}
);

-- 2. Hydrate & Mask using ts_hydrate_features
-- 'is_holiday' is NOT listed, so it passes through (Known Feature).
-- 'footfall' IS listed, so it becomes NULL in the test window (Unknown Feature).
CREATE TEMP TABLE safe_data AS
SELECT * FROM ts_hydrate_features(
    'cv_splits',            -- CV splits table
    'sales_data',           -- source table with features
    store_id,               -- group column
    date,                   -- date column
    MAP{}                   -- params
);

-- 3. Manually mask unknown features in test rows
-- (The _is_test flag from ts_hydrate_features tells us which rows are test)
CREATE TEMP TABLE masked_data AS
SELECT
    *,
    CASE WHEN _is_test THEN NULL ELSE footfall END AS footfall_safe
FROM safe_data;

-- 4. Fill the Unknowns (Imputation)
-- Since 'footfall' is now NULL in the test set, Regression would fail.
-- We fill it using the last observed value from the training set.
CREATE TEMP TABLE model_ready_data AS
SELECT * FROM ts_fill_unknown(
    'masked_data',          -- source table
    store_id,               -- group column
    date,                   -- date column
    footfall_safe,          -- column to fill
    (SELECT MAX(date) FROM masked_data WHERE split = 'train'),  -- cutoff
    MAP{'strategy': 'last_value'}  -- fill method
);

-- 5. Run the Backtest using anofox-statistics OLS
-- Note: ts_cv_forecast_by is for univariate models only.
-- For regression, use ts_prepare_regression_input + anofox-statistics:
LOAD anofox_statistics;

CREATE TEMP TABLE reg_input AS
SELECT * FROM ts_prepare_regression_input(
    'cv_splits', 'model_ready_data', store_id, date, revenue, MAP{}
);

SELECT
    ri.fold_id,
    ri.group_col AS store_id,
    ri.date_col AS date,
    ols.yhat AS forecast,
    ri.actual_target AS actual
FROM ols_fit_predict_by(
    'reg_input', fold_id, masked_target, [is_holiday, footfall_safe]
) ols
JOIN reg_input ri ON ols.row_id = ri.rowid
WHERE ri.split = 'test';
```

### Why It Works: Key Steps

| Step | Function | Purpose |
|------|----------|---------|
| 1 | `ts_cv_split` | Creates CV splits with train/test labels |
| 2 | `ts_hydrate_features` | Joins data with split info, provides `_is_test` flag |
| 3 | Manual masking | **Masks unknown features as NULL in test rows** |
| 4 | `ts_fill_unknown` | Imputes NULL values so regression can run |
| 5 | `ts_prepare_regression_input` + `ols_fit_predict_by` | Prepares data and runs regression (requires anofox-statistics) |

### Fill Methods

| Method | Description | Best For |
|--------|-------------|----------|
| `'last_value'` | Forward-fill from last training value | Slowly changing features |
| `'mean'` | Use training set mean | Stable, centered features |
| `'zero'` | Fill with zeros | Event indicators (default = no event) |
| `'linear'` | Linear interpolation | Trending features |

---

## Pro Tips: Best Practices

1. **Start Simple**
   Always run `ts_backtest_auto` with a baseline model (like `Naive` or `SeasonalNaive`) before trying complex regressions. If Naive beats your fancy model, something is wrong.

2. **Check the Gap**
   If your backtest accuracy is suspiciously high (e.g., 99% R-squared), you probably forgot to set a `gap` or mask a feature. Real-world accuracy is almost never that good.

3. **Scale Smart**
   For datasets >10M rows, use the composable pipeline (Pattern 4) to avoid running out of RAM. It creates pointers instead of copying data.

4. **Match Your Horizon**
   Your backtest `horizon` should match your production forecast window. Testing with `horizon=7` but deploying with `horizon=30` will give misleading results.

5. **Use Multiple Folds**
   Never trust a single fold. Use 3-5 folds minimum to get stable performance estimates and catch overfitting.

6. **Control Fold Spacing with skip_length**
   By default, folds are spaced by `horizon` periods. Use `skip_length` for custom spacing:
   - `skip_length: '1'` - Dense overlapping folds (more test coverage, slower)
   - `skip_length: '30'` - Sparse folds (faster, monthly checkpoints)

7. **Use clip_horizon for Recent Data**
   By default, folds that can't fit a full test window are skipped. Use `clip_horizon: 'true'` to include partial test windows at the end of your time series. Useful when you need to evaluate model performance on the most recent data.

---

## Troubleshooting / FAQ

### Q: Why is my forecast NULL?

**A:** You likely have 'Unknown' features in the test set that were not filled.

**Solution:** Use `ts_fill_unknown` after masking:

```sql
SELECT * FROM ts_fill_unknown(
    'your_masked_data',
    store_id,
    date,
    your_unknown_feature,
    cutoff_date,
    MAP{'strategy': 'last_value'}
);
```

---

### Q: Can I use different models for different groups?

**A:** Yes! The engine processes `group_col` independently. Each store/product/region gets its own model fit.

For explicit per-group model selection, you can:

```sql
-- Different models for different store types
SELECT * FROM ts_backtest_auto(
    'large_stores', store_id, date, revenue, 7, 5, '1d',
    MAP{'method': 'AutoARIMA'}
)
UNION ALL
SELECT * FROM ts_backtest_auto(
    'small_stores', store_id, date, revenue, 7, 5, '1d',
    MAP{'method': 'Theta'}
);
```

---

### Q: Why does my backtest accuracy drop in production?

**A:** Common causes:

| Cause | Solution |
|-------|----------|
| Missing `gap` parameter | Set `gap` to match your ETL latency |
| Leaking future features | Mask unknown features before forecasting |
| Different horizon | Match backtest horizon to production horizon |
| Concept drift | Use `window_type: 'fixed'` or `'sliding'` |

---

### Q: How do I know if I have data leakage?

**A:** Warning signs:
- R-squared > 0.95 on business data
- Test error much lower than training error
- Performance drops significantly in production

**Checklist:**
- [ ] Is `gap` set to match ETL latency?
- [ ] Are all unknown features masked?
- [ ] Is `embargo` set for overlapping targets?
- [ ] Are feature values truly available at forecast time?

---

## Quick Reference

| Pattern | Complexity | Use Case |
|---------|------------|----------|
| [Pattern 1](#pattern-1-the-quick-start) | Simple | Quick model evaluation |
| [Pattern 2](#pattern-2-regression-with-external-features) | Intermediate | External factors (requires anofox-statistics) |
| [Pattern 3](#pattern-3-production-reality) | Intermediate | ETL delays, label leakage prevention |
| [Pattern 4](#pattern-4-the-composable-pipeline) | Advanced | Custom pipelines, debugging |
| [Pattern 5](#pattern-5-unknown-vs-known-features-mask--fill) | Advanced | Complex feature handling (requires anofox-statistics) |
