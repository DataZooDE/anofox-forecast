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
-- ============================================
-- SAMPLE DATA: 3 stores, 90 days of daily sales
-- ============================================
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

-- ============================================
-- BACKTEST: Test AutoETS on 5 folds, 7-day horizon
-- ============================================
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
-- ============================================
-- SAMPLE DATA: Sales with external features
-- ============================================
CREATE OR REPLACE TABLE sales_data AS
SELECT
    'Store_' || LPAD(s::VARCHAR, 2, '0') AS store_id,
    '2024-01-01'::DATE + (d * INTERVAL '1 day') AS date,
    -- Features
    ROUND(15.0 + 10 * SIN(2 * PI() * d / 365) + (RANDOM() * 5), 1)::DOUBLE AS temperature,
    CASE WHEN d % 7 IN (0, 6) THEN 1 ELSE 0 END AS is_holiday,  -- Weekends
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

-- ============================================
-- REGRESSION BACKTEST (requires anofox-statistics)
-- ============================================
-- Install the statistics extension (run once)
INSTALL anofox_statistics FROM community;
LOAD anofox_statistics;

-- 1. Create CV splits
CREATE OR REPLACE TEMP TABLE cv_splits AS
SELECT * FROM ts_cv_split(
    'sales_data', store_id, date, revenue,
    ['2024-02-15', '2024-03-01']::DATE[],  -- 2 folds
    7, '1d', MAP{}
);

-- 2. Prepare regression input (masks target as NULL for test rows)
--    Note: revenue column is preserved as-is for scoring; masked_target is NULL for test
CREATE OR REPLACE TEMP TABLE reg_input AS
SELECT * FROM ts_prepare_regression_input(
    'cv_splits', 'sales_data', store_id, date, revenue, MAP{}
);

-- 3. Run OLS fit-predict with multiple features
--    OLS results maintain input order within each group, so we match by row number
WITH
reg_input_numbered AS (
    SELECT
        ROW_NUMBER() OVER (PARTITION BY fold_id ORDER BY group_col, date_col) AS row_in_fold,
        *
    FROM reg_input
),
ols_raw AS (
    SELECT
        ROW_NUMBER() OVER (PARTITION BY group_id ORDER BY group_id) AS row_in_fold,
        group_id AS fold_id,
        yhat AS forecast
    FROM ols_fit_predict_by(
        'reg_input',
        fold_id,
        masked_target,
        [temperature, is_holiday, promotion_active]
    )
)
SELECT
    ri.fold_id,
    ri.group_col AS store_id,
    ri.date_col AS date,
    ROUND(ols.forecast, 2) AS forecast,
    ri.revenue AS actual,
    ROUND(ols.forecast - ri.revenue, 2) AS error,
    ROUND(ABS(ols.forecast - ri.revenue), 2) AS abs_error
FROM ols_raw ols
JOIN reg_input_numbered ri ON ols.fold_id = ri.fold_id AND ols.row_in_fold = ri.row_in_fold
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
-- ============================================
-- SAMPLE DATA: 3 stores, 90 days of daily sales
-- ============================================
CREATE OR REPLACE TABLE sales_data AS
SELECT
    'Store_' || LPAD(s::VARCHAR, 2, '0') AS store_id,
    '2024-01-01'::DATE + (d * INTERVAL '1 day') AS date,
    ROUND(
        100.0 + s * 20.0
        + 0.3 * d
        + 15 * SIN(2 * PI() * d / 7)
        + (RANDOM() * 10 - 5)
    , 2)::DOUBLE AS revenue
FROM generate_series(0, 89) AS t(d)
CROSS JOIN generate_series(1, 3) AS s(s);

-- ============================================
-- BACKTEST: With gap to simulate ETL latency
-- ============================================
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
-- ============================================
-- SAMPLE DATA: 3 stores, 90 days of daily sales
-- ============================================
CREATE OR REPLACE TABLE sales_data AS
SELECT
    'Store_' || LPAD(s::VARCHAR, 2, '0') AS store_id,
    '2024-01-01'::DATE + (d * INTERVAL '1 day') AS date,
    ROUND(
        100.0 + s * 20.0
        + 0.3 * d
        + 15 * SIN(2 * PI() * d / 7)
        + (RANDOM() * 10 - 5)
    , 2)::DOUBLE AS revenue
FROM generate_series(0, 89) AS t(d)
CROSS JOIN generate_series(1, 3) AS s(s);

-- ============================================
-- COMPOSABLE PIPELINE: Step-by-step backtest
-- ============================================

-- Step 1: Define the fold boundaries (Metadata only)
-- This generates cutoff dates without touching your data
CREATE OR REPLACE TEMP TABLE fold_meta AS
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
CREATE OR REPLACE TEMP TABLE cv_splits AS
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
CREATE OR REPLACE TEMP TABLE train_splits AS
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
-- ============================================
-- SAMPLE DATA: Sales with known and unknown features
-- ============================================
CREATE OR REPLACE TABLE sales_data AS
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
        + 0.5 * (100 + 50 * CASE WHEN (d % 7) IN (0, 6) THEN 1 ELSE 0 END)  -- footfall effect
        + 20.0 * CASE WHEN (d % 7) IN (0, 6) THEN 1 ELSE 0 END              -- holiday effect
        + (RANDOM() * 10 - 5)
    , 2)::DOUBLE AS revenue
FROM generate_series(0, 89) AS t(d)
CROSS JOIN generate_series(1, 2) AS s(s);

-- ============================================
-- FEATURE MASKING PIPELINE
-- ============================================

-- 1. Create the CV splits
CREATE OR REPLACE TEMP TABLE cv_splits AS
SELECT * FROM ts_cv_split(
    'sales_data', store_id, date, revenue,
    ['2024-02-15', '2024-03-01']::DATE[],  -- training end times
    7, '1d', MAP{}
);

-- 2. Hydrate & Mask using ts_hydrate_features
-- 'is_holiday' is NOT listed, so it passes through (Known Feature).
-- 'footfall' IS listed, so it becomes NULL in the test window (Unknown Feature).
CREATE OR REPLACE TEMP TABLE safe_data AS
SELECT * FROM ts_hydrate_features(
    'cv_splits',            -- CV splits table
    'sales_data',           -- source table with features
    store_id,               -- group column
    date,                   -- date column
    MAP{}                   -- params
);

-- 3. Manually mask unknown features in test rows
-- (The _is_test flag from ts_hydrate_features tells us which rows are test)
CREATE OR REPLACE TEMP TABLE masked_data AS
SELECT
    *,
    CASE WHEN _is_test THEN NULL ELSE footfall END AS footfall_safe
FROM safe_data;

-- 4. Fill the Unknowns (Imputation)
-- ts_fill_unknown returns (group_col, date_col, value_col) - join back to get full data
CREATE OR REPLACE TEMP TABLE filled_footfall AS
SELECT * FROM ts_fill_unknown(
    'masked_data',          -- source table
    store_id,               -- group column
    date,                   -- date column
    footfall_safe,          -- column to fill
    (SELECT MAX(date) FROM masked_data WHERE split = 'train'),  -- cutoff
    MAP{'strategy': 'last_value'}  -- fill method
);

-- Join filled values back to masked_data
CREATE OR REPLACE TEMP TABLE model_ready_data AS
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

-- 5. Run the Backtest using anofox-statistics OLS
INSTALL anofox_statistics FROM community;
LOAD anofox_statistics;

-- Prepare regression input using model_ready_data
-- Note: We use the cv_splits structure with features from model_ready_data
CREATE OR REPLACE TEMP TABLE reg_input AS
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

-- OLS results maintain input order within each group
CREATE OR REPLACE TEMP TABLE ols_predictions AS
WITH
reg_input_numbered AS (
    SELECT
        ROW_NUMBER() OVER (PARTITION BY fold_id ORDER BY group_col, date_col) AS row_in_fold,
        *
    FROM reg_input
),
ols_raw AS (
    SELECT
        ROW_NUMBER() OVER (PARTITION BY group_id ORDER BY group_id) AS row_in_fold,
        group_id AS fold_id,
        yhat AS forecast
    FROM ols_fit_predict_by(
        'reg_input', fold_id, masked_target, [is_holiday, footfall_filled]
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

-- Calculate metrics per fold using built-in functions
SELECT
    fold_id,
    COUNT(*) AS n_predictions,
    ROUND(ts_mae(LIST(actual), LIST(forecast)), 2) AS mae,
    ROUND(ts_rmse(LIST(actual), LIST(forecast)), 2) AS rmse,
    ROUND(ts_bias(LIST(actual), LIST(forecast)), 2) AS bias
FROM ols_predictions
GROUP BY fold_id
ORDER BY fold_id;
```

### Why It Works: Key Steps

| Step | Function | Purpose |
|------|----------|---------|
| 1 | `ts_cv_split` | Creates CV splits with train/test labels |
| 2 | `ts_hydrate_features` | Joins data with split info, provides `_is_test` flag |
| 3 | Manual masking | **Masks unknown features as NULL in test rows** |
| 4 | `ts_fill_unknown` + JOIN | Fills NULL values and joins back to original data |
| 5 | Manual `reg_input` + `ols_fit_predict_by` | Constructs regression input and runs OLS (requires anofox-statistics) |

### Fill Methods

| Method | Description | Best For |
|--------|-------------|----------|
| `'last_value'` | Forward-fill from last training value | Slowly changing features |
| `'mean'` | Use training set mean | Stable, centered features |
| `'zero'` | Fill with zeros | Event indicators (default = no event) |
| `'linear'` | Linear interpolation | Trending features |

---

## Tips

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
-- Create sample data for different store types
CREATE OR REPLACE TABLE large_stores AS
SELECT 'Large_' || s AS store_id, '2024-01-01'::DATE + d AS date,
       ROUND(500 + 50*SIN(2*PI()*d/7) + RANDOM()*20, 2)::DOUBLE AS revenue
FROM generate_series(0, 59) t(d) CROSS JOIN generate_series(1, 2) s(s);

CREATE OR REPLACE TABLE small_stores AS
SELECT 'Small_' || s AS store_id, '2024-01-01'::DATE + d AS date,
       ROUND(100 + 10*SIN(2*PI()*d/7) + RANDOM()*5, 2)::DOUBLE AS revenue
FROM generate_series(0, 59) t(d) CROSS JOIN generate_series(1, 2) s(s);

-- Different models for different store types
SELECT * FROM ts_backtest_auto(
    'large_stores', store_id, date, revenue, 7, 3, '1d',
    MAP{'method': 'AutoARIMA'}
)
UNION ALL
SELECT * FROM ts_backtest_auto(
    'small_stores', store_id, date, revenue, 7, 3, '1d',
    MAP{'method': 'Theta'}
);
```

