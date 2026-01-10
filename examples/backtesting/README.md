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
    source      => 'sales_data',
    group_col   => 'store_id',
    date_col    => 'date',
    target_col  => 'revenue',
    horizon     => 7,           -- Forecast next 7 days
    folds       => 5,           -- Test on 5 different historical periods
    frequency   => '1d',
    params      => MAP{'model': 'AutoETS'},
    features    => NULL,        -- No external factors
    metric      => 'rmse'
);
```

### Why It Works: Key Parameters

| Parameter | Value | Explanation |
|-----------|-------|-------------|
| `horizon => 7` | 7 days | Matches your production forecast window |
| `folds => 5` | 5 folds | Tests across 5 different time periods for robust evaluation |
| `features => NULL` | No features | **The model only looks at its own history** - pure univariate forecasting |
| `metric => 'rmse'` | RMSE | Returns `fold_metric_score` column with RMSE per fold |

> **Key Insight:** When `features => NULL`, the model uses only the target column's historical values. This is ideal for series with strong autocorrelation but no known external drivers.

---

## Pattern 2: Multivariate Regression

### The Scenario

Your sales depend on temperature, holidays, and promotions. Standard ARIMA might fail because it ignores these factors. You need a regression model.

### The SQL Code

```sql
-- When sales depend on external factors, use regression
SELECT * FROM ts_backtest_auto(
    source      => 'sales_data',
    group_col   => 'store_id',
    date_col    => 'date',
    target_col  => 'revenue',
    horizon     => 7,
    folds       => 5,
    frequency   => '1d',
    -- 1. Pass the column names of your external features
    features    => ['temperature', 'is_holiday', 'promotion_active'],
    -- 2. Select a regression model
    params      => MAP{'model': 'ols'},
    -- 3. Choose your preferred error metric
    metric      => 'mae'
);
```

### Why It Works: Key Parameters

| Parameter | Value | Explanation |
|-----------|-------|-------------|
| `features => [...]` | 3 columns | External regressors included in the model |
| `params => MAP{'model': 'ols'}` | OLS | **Switches from recursive forecasting to direct regression** |
| `metric => 'mae'` | MAE | Mean Absolute Error - more robust to outliers than RMSE |

> **Key Insight:** Setting `model: 'ols'` changes the forecasting engine entirely. Instead of fitting an ARIMA-style model and recursively forecasting, it fits a linear regression where `revenue = f(temperature, is_holiday, promotion_active)`.

---

## Pattern 3: Production Reality

### The Scenario

In the real world, you rarely have today's data available immediately. Your ETL might take 2 days. Your target might be a rolling 7-day average that overlaps test windows.

### The SQL Code

```sql
-- Simulate real-world data latency and prevent label leakage
SELECT * FROM ts_backtest_auto(
    source      => 'sales_data',
    group_col   => 'store_id',
    date_col    => 'date',
    target_col  => 'revenue',
    horizon     => 7,
    folds       => 5,
    frequency   => '1d',
    params      => MAP{
        'model': 'AutoARIMA',
        'gap': '2',      -- Skip 2 days between Train end and Test start
        'embargo': '0'   -- No embargo needed for point forecasts
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
    source_table       => 'sales_data',
    date_col           => 'date',
    n_folds            => 3,
    horizon            => 7,
    frequency          => '1d',
    params             => MAP{'gap': '1'}
);

-- Step 2: Create the split index (Efficient - pointers only, no data copying)
-- Links each row to its fold and train/test assignment
CREATE TEMP TABLE split_idx AS
SELECT * FROM ts_cv_split_index(
    source_table       => 'sales_data',
    group_col          => 'store_id',
    date_col           => 'date',
    fold_info_table    => 'fold_meta',
    params             => MAP{}
);

-- Step 3: Hydrate the data (Safe Join - Masks future 'unknown' features)
-- This creates the actual table used for training
CREATE TEMP TABLE training_data AS
SELECT * FROM ts_hydrate_split(
    split_table        => 'split_idx',
    source_table       => 'sales_data',
    group_col          => 'store_id',
    date_col           => 'date',
    unknown_features   => ['temperature'] -- These will be NULL in test rows
);

-- Step 4: Run the forecast on the prepared data
SELECT * FROM ts_cv_forecast_by(
    input_table        => 'training_data',
    group_col          => 'store_id',
    date_col           => 'date',
    target_col         => 'revenue',
    method             => 'AutoETS',
    horizon            => 7,
    params             => MAP{}
);
```

### Why It Works: Key Steps

| Step | Function | Purpose |
|------|----------|---------|
| 1 | `ts_cv_generate_folds` | Generate fold cutoff dates only (no data movement) |
| 2 | `ts_cv_split_index` | Create index mapping rows to folds (memory efficient) |
| 3 | `ts_hydrate_split` | Join data with split info, masking unknown features |
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
│  AFTER MASKING (ts_hydrate_split with unknown_features => ['footfall'])    │
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
│  AFTER FILLING (ts_fill_unknown with method => 'last_value')               │
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
-- 1. Create the Split Index
CREATE TEMP TABLE split_idx AS
SELECT * FROM ts_cv_split_index('sales_data', 'store_id', 'date', 'fold_meta', MAP{});

-- 2. Hydrate & Mask
-- 'is_holiday' is NOT listed, so it passes through (Known Feature).
-- 'footfall' IS listed, so it becomes NULL in the test window (Unknown Feature).
CREATE TEMP TABLE safe_data AS
SELECT * FROM ts_hydrate_split(
    split_table      => 'split_idx',
    source_table     => 'sales_data',
    group_col        => 'store_id',
    date_col         => 'date',
    unknown_features => ['footfall'] -- <--- Crucial Step: Hide the future
);

-- 3. Fill the Unknowns (Imputation)
-- Since 'footfall' is now NULL in the test set, Regression would fail.
-- We fill it using the last observed value from the training set.
CREATE TEMP TABLE model_ready_data AS
SELECT * FROM ts_fill_unknown(
    input_table      => 'safe_data',
    group_col        => 'store_id',
    date_col         => 'date',
    columns          => ['footfall'],
    method           => 'last_value' -- Options: 'mean', 'zero', 'linear'
);

-- 4. Run the Backtest
-- Now the model uses "Planned Holiday" and "Estimated Footfall"
SELECT * FROM ts_cv_forecast_by(
    input_table      => 'model_ready_data',
    group_col        => 'store_id',
    date_col         => 'date',
    target_col       => 'revenue',
    method           => 'ols',       -- Multivariate Regression
    horizon          => 7,
    params           => MAP{}
);
```

### Why It Works: Key Steps

| Step | Function | Purpose |
|------|----------|---------|
| 1 | `ts_cv_split_index` | Creates row-to-fold mapping |
| 2 | `ts_hydrate_split` | Joins data and **masks unknown features as NULL in test rows** |
| 3 | `ts_fill_unknown` | Imputes NULL values so regression can run |
| 4 | `ts_cv_forecast_by` | Runs model with safe, imputed data |

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
   For datasets >10M rows, use `ts_cv_split_index` (Pattern 4) to avoid running out of RAM. It creates pointers instead of copying data.

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

**Solution:** Use `ts_fill_unknown` after `ts_hydrate_split`:

```sql
SELECT * FROM ts_fill_unknown(
    input_table => 'your_hydrated_data',
    group_col   => 'store_id',
    date_col    => 'date',
    columns     => ['your_unknown_feature'],
    method      => 'last_value'
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
    MAP{'model': 'AutoARIMA'}
)
UNION ALL
SELECT * FROM ts_backtest_auto(
    'small_stores', store_id, date, revenue, 7, 5, '1d',
    MAP{'model': 'Theta'}
);
```

---

### Q: Why does my backtest accuracy drop in production?

**A:** Common causes:

| Cause | Solution |
|-------|----------|
| Missing `gap` parameter | Set `gap` to match your ETL latency |
| Leaking future features | List unknown features in `ts_hydrate_split` |
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
| [Pattern 2](#pattern-2-multivariate-regression) | Simple | External factors (weather, promotions) |
| [Pattern 3](#pattern-3-production-reality) | Intermediate | ETL delays, label leakage prevention |
| [Pattern 4](#pattern-4-the-composable-pipeline) | Advanced | Custom pipelines, debugging |
| [Pattern 5](#pattern-5-unknown-vs-known-features-mask--fill) | Advanced | Complex feature handling |
