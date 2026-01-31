# Cross-Validation & Backtesting

> Time series cross-validation with proper temporal ordering

## Overview

Time series cross-validation requires special handling because data has temporal ordering. These functions help you create proper train/test splits, handle unknown features during backtesting, and prevent data leakage.

**Use this document to:**
- Run complete backtests with a single function call (`ts_backtest_auto_by`)
- Create proper expanding window train/test splits with temporal ordering
- Generate cross-validation folds for custom forecasting pipelines
- Compare model performance across multiple time periods
- Prevent data leakage by ensuring test data is always in the future

### Data Preparation Requirement

**Important:** Cross-validation functions assume your data is **pre-cleaned**:
- No missing dates/gaps in the time series
- Consistent frequency throughout each series
- Data sorted by date within each group

These functions use **position-based fold assignment** (not date arithmetic), which correctly handles all frequency types including calendar-based frequencies (monthly, quarterly, yearly) where intervals are irregular.

If your data has gaps or irregular frequency, use [`ts_fill_gaps_by`](04-data-preparation.md#ts_fill_gaps_by) to prepare it before running cross-validation:

```sql
-- Prepare data: fill gaps and ensure consistent frequency
CREATE TABLE prepared_data AS
SELECT * FROM ts_fill_gaps_by('raw_data', series_id, date, value, '1mo', MAP{});

-- Then run backtest on prepared data
SELECT * FROM ts_backtest_auto_by('prepared_data', series_id, date, value, 6, 3, '1mo', MAP{});
```

---

## Quick Start

### One-Liner (Recommended)

For most use cases, use `ts_backtest_auto_by` - complete backtesting in a single call:

```sql
-- Backtest Naive (no seasonality required)
SELECT * FROM ts_backtest_auto_by(
    'sales_data', store_id, date, revenue,
    7, 5, '1d', {'method': 'Naive'}
);

-- Aggregate results
SELECT
    model_name,
    AVG(abs_error) AS mae,
    AVG(fold_metric_score) AS avg_rmse
FROM ts_backtest_auto_by('sales_data', store_id, date, revenue, 7, 5, '1d', {'method': 'Naive'})
GROUP BY model_name;
```

**For seasonal data**, first detect the period, then pass it explicitly:

```sql
-- Step 1: Detect seasonality
SELECT * FROM ts_detect_periods_by('sales_data', store_id, date, revenue, MAP{});
-- Returns: primary_period = 7 (weekly)

-- Step 2: Backtest with seasonal model
SELECT * FROM ts_backtest_auto_by(
    'sales_data', store_id, date, revenue,
    7, 5, '1d', {'method': 'AutoETS', 'seasonal_period': 7}
);
```

### Modular Approach

For custom pipelines or regression models (using `anofox_statistics` extension):

```sql
-- Requires: LOAD 'anofox_statistics';

-- 1. Generate fold boundaries
WITH folds AS (
    SELECT training_end_times FROM ts_cv_generate_folds(data, date, 7, 5, MAP{})
)

-- 2. Create train/test splits
SELECT * FROM ts_cv_split_by('data', 'store_id', 'date', 'revenue',
    (SELECT training_end_times FROM folds), 7, '1d', MAP{});

-- 3. Apply linear regression from anofox_statistics on each fold
-- Train on 'train' split, predict on 'test' split
WITH splits AS (
    SELECT * FROM ts_cv_split_by('data', 'store_id', 'date', 'revenue',
        (SELECT training_end_times FROM ts_cv_generate_folds(data, date, 7, 5, MAP{})),
        7, '1d', MAP{})
)
SELECT
    fold_id,
    store_id,
    linear_regression_predict(features, coefficients) AS forecast
FROM splits
WHERE split = 'test';
```

### Usage Pattern Comparison

| Pattern | Use Case | Complexity |
|---------|----------|------------|
| **One-liner** (`ts_backtest_auto_by`) | Quick evaluation, 80% of use cases | Simple |
| **Modular** (`ts_cv_split_by` + `ts_cv_forecast_by`) | Custom pipelines, regression models | Advanced |

---

## Table Macros

### ts_backtest_auto_by

Complete backtesting in a single function call.

**Signature:**
```sql
ts_backtest_auto_by(
    source VARCHAR,
    group_col COLUMN,
    date_col COLUMN,
    target_col COLUMN,
    horizon BIGINT,
    folds BIGINT,
    frequency VARCHAR,
    params MAP or STRUCT,
    features VARCHAR[],      -- Optional
    metric VARCHAR           -- Optional, default 'rmse'
) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Table containing time series data |
| `group_col` | COLUMN | Series identifier column |
| `date_col` | COLUMN | Date/timestamp column |
| `target_col` | COLUMN | Target value to forecast |
| `horizon` | BIGINT | Number of periods to forecast ahead |
| `folds` | BIGINT | Number of CV folds |
| `frequency` | VARCHAR | Data frequency (`'1d'`, `'1h'`, `'1w'`, `'1mo'`) |
| `params` | MAP/STRUCT | Model and CV parameters |
| `features` | VARCHAR[] | Optional regressor columns |
| `metric` | VARCHAR | Metric for fold_metric_score (default `'rmse'`) |

**Params Options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `method` | VARCHAR | `'AutoETS'` | Forecasting model |
| `gap` | BIGINT | `0` | Periods between train end and test start |
| `embargo` | BIGINT | `0` | Periods to exclude after previous test |
| `window_type` | VARCHAR | `'expanding'` | `'expanding'`, `'fixed'`, or `'sliding'` |
| `min_train_size` | BIGINT | `1` | Minimum training periods |
| `initial_train_size` | BIGINT | 50% | Periods for first fold |
| `skip_length` | BIGINT | `horizon` | Periods between fold starts |
| `clip_horizon` | BOOLEAN | `false` | Include partial test windows |

**Supported Metrics:**
| Metric | Parameter | Description |
|--------|-----------|-------------|
| RMSE | `'rmse'` | Root Mean Squared Error (default) |
| MAE | `'mae'` | Mean Absolute Error |
| MAPE | `'mape'` | Mean Absolute Percentage Error |
| MSE | `'mse'` | Mean Squared Error |
| SMAPE | `'smape'` | Symmetric MAPE |
| Bias | `'bias'` | Mean Error |
| R² | `'r2'` | Coefficient of Determination |
| Coverage | `'coverage'` | Prediction interval coverage |

**Output Columns:**
| Column | Type | Description |
|--------|------|-------------|
| `fold_id` | BIGINT | CV fold number |
| `group_col` | ANY | Series identifier |
| `date` | TIMESTAMP | Forecast date |
| `forecast` | DOUBLE | Point forecast |
| `actual` | DOUBLE | Actual value |
| `error` | DOUBLE | forecast - actual |
| `abs_error` | DOUBLE | |forecast - actual| |
| `lower_90` | DOUBLE | Lower 90% prediction interval |
| `upper_90` | DOUBLE | Upper 90% prediction interval |
| `model_name` | VARCHAR | Model used |
| `fold_metric_score` | DOUBLE | Calculated metric for fold |

**Examples:**
```sql
-- Basic backtest with AutoETS
SELECT * FROM ts_backtest_auto_by(
    'sales_data', store_id, date, revenue,
    7, 5, '1d', {'method': 'AutoETS'}
);

-- With STRUCT params (mixed types)
SELECT * FROM ts_backtest_auto_by(
    'sales_data', store_id, date, revenue, 7, 5, '1d',
    {'method': 'Naive', 'gap': 2, 'clip_horizon': true}
);

-- Different metric
SELECT * FROM ts_backtest_auto_by(
    'sales_data', store_id, date, revenue, 7, 5, '1d',
    {'method': 'Theta'},
    NULL, 'smape'
);
```

---

### ts_cv_generate_folds

Automatically generate fold boundaries based on data range.

**Signature:**
```sql
ts_cv_generate_folds(
    source TABLE or VARCHAR,  -- Source table or table name
    date_col COLUMN,          -- Date column (unquoted identifier)
    n_folds BIGINT,
    horizon BIGINT,
    params MAP                -- Optional: {initial_train_size, skip_length, clip_horizon}
) → TABLE(training_end_times DATE[] or TIMESTAMP[])
```

> **Important:** Assumes pre-cleaned data with no gaps. Use `ts_fill_gaps_by` first if your data has missing dates.
> Uses position-based indexing internally, so no frequency parameter is needed.

**Example:**
```sql
SELECT training_end_times
FROM ts_cv_generate_folds('sales_data', date, 3, 5, MAP{});
-- Returns: ['2024-01-15', '2024-01-20', '2024-01-25'] (preserves original date type)
```

---

### ts_cv_split_folds_by

View fold date ranges (train/test boundaries).

**Signature:**
```sql
ts_cv_split_folds_by(
    source VARCHAR,
    group_col VARCHAR,
    date_col VARCHAR,
    training_end_times DATE[],
    horizon BIGINT,
    frequency VARCHAR
) → TABLE
```

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `fold_id` | BIGINT | Fold number |
| `train_start` | TIMESTAMP | Training period start |
| `train_end` | TIMESTAMP | Training period end |
| `test_start` | TIMESTAMP | Test period start |
| `test_end` | TIMESTAMP | Test period end |
| `horizon` | BIGINT | Test period length |

---

### ts_cv_split_by

Split time series data into train/test sets.

**Signature:**
```sql
ts_cv_split_by(
    source VARCHAR,           -- Source table name (quoted string)
    group_col IDENTIFIER,     -- Series grouping column (unquoted)
    date_col IDENTIFIER,      -- Date/timestamp column (unquoted)
    target_col IDENTIFIER,    -- Value column (unquoted)
    training_end_times DATE[],
    horizon BIGINT,
    frequency VARCHAR,
    params MAP
) → TABLE
```

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier (preserves original column name) |
| `<date_col>` | (same as input) | Date/timestamp (preserves original column name) |
| `<target_col>` | (same as input) | Target value (preserves original column name) |
| `fold_id` | BIGINT | Fold number (1, 2, 3, ...) |
| `split` | VARCHAR | `'train'` or `'test'` |

**Window Types:**
```
Expanding window (default):
Fold 1: [====TRAIN====][TEST]
Fold 2: [======TRAIN======][TEST]
Fold 3: [========TRAIN========][TEST]

Fixed window:
Fold 1:     [==TRAIN==][TEST]
Fold 2:         [==TRAIN==][TEST]
Fold 3:             [==TRAIN==][TEST]
```

**Example:**
```sql
CREATE TABLE cv_splits AS
SELECT * FROM ts_cv_split_by('sales', store_id, date, revenue,
    ['2024-01-10'::DATE, '2024-01-15'::DATE], 5, '1d', MAP{});
-- Output columns: store_id, date, revenue, fold_id, split
```

---

### ts_cv_split_index_by

Memory-efficient alternative returning only index columns (no target values).

**Signature:**
```sql
ts_cv_split_index_by(
    source VARCHAR,           -- Source table name (quoted string)
    group_col IDENTIFIER,     -- Series grouping column (unquoted)
    date_col IDENTIFIER,      -- Date/timestamp column (unquoted)
    training_end_times DATE[],
    horizon BIGINT,
    frequency VARCHAR,
    params MAP
) → TABLE
```

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier (preserves original column name) |
| `<date_col>` | (same as input) | Date/timestamp (preserves original column name) |
| `fold_id` | BIGINT | Fold number |
| `split` | VARCHAR | `'train'` or `'test'` |

---

### ts_cv_forecast_by

Generate forecasts for all CV folds.

**Signature:**
```sql
ts_cv_forecast_by(
    cv_splits VARCHAR,        -- CV splits table name (quoted string)
    group_col IDENTIFIER,     -- Series grouping column (unquoted)
    date_col IDENTIFIER,      -- Date/timestamp column (unquoted)
    target_col IDENTIFIER,    -- Value column (unquoted)
    method VARCHAR,
    horizon BIGINT,
    params MAP,
    frequency VARCHAR
) → TABLE
```

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `fold_id` | BIGINT | Fold number |
| `<group_col>` | (same as input) | Series identifier (preserves original column name) |
| `forecast_step` | INTEGER | Forecast horizon step (1, 2, 3, ...) |
| `<date_col>` | (same as input) | Forecast date (preserves original column name) |
| `point_forecast` | DOUBLE | Point forecast value |
| `lower_90` | DOUBLE | Lower 90% prediction interval bound |
| `upper_90` | DOUBLE | Upper 90% prediction interval bound |
| `model_name` | VARCHAR | Model used for this forecast |

**Example:**
```sql
-- Create splits
CREATE TABLE cv_splits AS
SELECT * FROM ts_cv_split_by('sales', store_id, date, revenue,
    ['2024-01-10'::DATE, '2024-01-15'::DATE], 5, '1d', MAP{});

-- Generate forecasts (use same column names as cv_splits)
SELECT * FROM ts_cv_forecast_by(
    'cv_splits',
    store_id, date, revenue,
    'AutoETS', 5, MAP{}, '1d'
);
-- Output columns: fold_id, store_id, forecast_step, date, point_forecast, lower_90, upper_90, model_name
```

---

## Advanced: CV Data Hydration

> These functions are for building custom cross-validation pipelines.
> For standard backtesting, use `ts_backtest_auto_by` which handles everything automatically.

When building custom CV pipelines (e.g., with regression models or external forecasters), you need to join CV splits back to your source data. These functions prevent **data leakage** by automatically masking features that wouldn't be known at prediction time.

### The Data Leakage Problem

In time series CV, the test set represents "future" data. Features that depend on future values (e.g., actual temperature, competitor sales) must be masked to prevent leakage:

```
Timeline:    [====== TRAIN ======][=== TEST ===]
                                  ↑
                            cutoff date

Known features:     ✓ Available in both train and test
Unknown features:   ✓ Available in train, ✗ Must be masked in test
```

---

### ts_hydrate_split_by

Join CV splits with source data, masking a specific unknown column.

**Signature:**
```sql
ts_hydrate_split_by(
    cv_splits VARCHAR,        -- CV split table name
    source VARCHAR,           -- Source data table name
    src_group_col COLUMN,     -- Group column in source
    src_date_col COLUMN,      -- Date column in source
    unknown_col COLUMN,       -- Column to mask in test set
    params MAP                -- Masking strategy options
) → TABLE
```

**Params Options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `strategy` | VARCHAR | `'null'` | `'null'`, `'last_value'`, or `'default'` |
| `fill_value` | DOUBLE | `0.0` | Value for `'default'` strategy |

**Masking Strategies:**
| Strategy | Description |
|----------|-------------|
| `'null'` | Set unknown values to NULL (default) |
| `'last_value'` | Use last known value before cutoff |
| `'default'` | Use specified `fill_value` |

**Example:**
```sql
-- Mask temperature in test set using last known value
SELECT * FROM ts_hydrate_split_by(
    'cv_splits',
    'weather_data',
    region,
    date,
    temperature,
    {'strategy': 'last_value'}
) WHERE fold_id = 1;
```

---

### ts_hydrate_split_full_by

Join CV splits with ALL source columns, adding metadata flags for manual masking.

**Signature:**
```sql
ts_hydrate_split_full_by(
    cv_splits VARCHAR,
    source VARCHAR,
    src_group_col COLUMN,
    src_date_col COLUMN,
    params MAP
) → TABLE
```

**Output Columns:**
| Column | Type | Description |
|--------|------|-------------|
| `fold_id` | BIGINT | CV fold number |
| `split` | VARCHAR | `'train'` or `'test'` |
| `_is_test` | BOOLEAN | True for test rows |
| `_train_cutoff` | TIMESTAMP | Training end date for fold |
| *(all source columns)* | | Original data columns |

**Example:**
```sql
-- Manual masking pattern
SELECT
    *,
    CASE WHEN _is_test THEN NULL ELSE competitor_sales END AS competitor_sales_masked,
    CASE WHEN _is_test THEN NULL ELSE actual_weather END AS weather_masked
FROM ts_hydrate_split_full_by(
    'cv_splits', 'sales_features', store_id, date, MAP{}
);
```

---

### ts_hydrate_split_strict_by

Fail-safe join returning ONLY metadata columns, forcing explicit column selection.

**Signature:**
```sql
ts_hydrate_split_strict_by(
    cv_splits VARCHAR,
    source VARCHAR,
    src_group_col COLUMN,
    src_date_col COLUMN,
    params MAP
) → TABLE(fold_id, split, group_col, date_col, _is_test, _train_cutoff)
```

**Example:**
```sql
-- Maximum safety: explicitly select and mask each column
SELECT
    hs.*,
    src.price,  -- Known feature, no masking needed
    CASE WHEN hs._is_test THEN NULL ELSE src.competitor_price END AS competitor_price
FROM ts_hydrate_split_strict_by('cv_splits', 'data', store, date, MAP{}) hs
JOIN data src ON hs.group_col = src.store AND hs.date_col = src.date;
```

---

### ts_hydrate_features_by

Automatically hydrate CV splits with features, marking test rows for masking.

**Signature:**
```sql
ts_hydrate_features_by(
    cv_splits VARCHAR,        -- Output from ts_cv_split
    source VARCHAR,           -- Original data with features
    src_group_col COLUMN,
    src_date_col COLUMN,
    params MAP
) → TABLE
```

**Output Columns:** Same as `ts_hydrate_split_full_by` including `_is_test` flag.

**Example:**
```sql
-- Hydrate and mask unknown features
SELECT
    *,
    CASE WHEN _is_test THEN NULL ELSE temperature END AS temp_masked,
    CASE WHEN _is_test THEN NULL ELSE competitor_sales END AS comp_masked
FROM ts_hydrate_features_by('cv_splits', 'feature_data', series_id, date, MAP{});
```

---

### ts_prepare_regression_input_by

Prepare data for regression models in CV backtest. Masks target in test rows.

**Signature:**
```sql
ts_prepare_regression_input_by(
    cv_splits VARCHAR,
    source VARCHAR,
    src_group_col COLUMN,
    src_date_col COLUMN,
    target_col COLUMN,
    params MAP
) → TABLE
```

**Key Behavior:**
- **Train rows:** Target keeps original value
- **Test rows:** Target set to NULL (model will predict these)
- All features from source are preserved

**Output Columns:**
| Column | Type | Description |
|--------|------|-------------|
| `fold_id` | BIGINT | CV fold number |
| `split` | VARCHAR | `'train'` or `'test'` |
| `group_col` | ANY | Group identifier |
| `date_col` | TIMESTAMP | Date |
| `masked_target` | DOUBLE | NULL in test, actual in train |
| `_is_test` | BOOLEAN | Test row indicator |
| *(all source columns)* | | Original features |

**Example:**
```sql
-- Prepare data for external regression model
CREATE TABLE regression_input AS
SELECT * FROM ts_prepare_regression_input_by(
    'cv_splits', 'sales_data', store_id, date, revenue, MAP{}
);

-- Train: Use rows where masked_target IS NOT NULL
-- Test: Predict rows where masked_target IS NULL
```

---

### Choosing the Right Hydration Function

| Function | Use Case | Safety Level |
|----------|----------|--------------|
| `ts_hydrate_split_by` | Mask one specific column | Medium |
| `ts_hydrate_split_full_by` | Manual masking with `_is_test` flag | Medium |
| `ts_hydrate_split_strict_by` | Force explicit column handling | High |
| `ts_hydrate_features_by` | Auto-hydrate with masking flags | Medium |
| `ts_prepare_regression_input_by` | Regression model prep | High |

---

*See also: [Forecasting](07-forecasting.md) | [Evaluation Metrics](09-evaluation-metrics.md)*
