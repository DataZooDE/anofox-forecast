# Cross-Validation & Backtesting

> Time series cross-validation with proper temporal ordering

## Overview

Time series cross-validation requires special handling because data has temporal ordering. These functions help you create proper train/test splits, handle unknown features during backtesting, and prevent data leakage.

## Function Overview

| Function | Purpose |
|----------|---------|
| `ts_backtest_auto` | **One-liner backtest** - complete CV in a single call |
| `ts_cv_generate_folds` | Auto-generate fold boundaries based on data range |
| `ts_cv_split_folds` | View fold date ranges (train/test boundaries) |
| `ts_cv_split` | Create train/test splits with fold assignments |
| `ts_cv_forecast_by` | Generate forecasts for all CV folds |

## Two Usage Patterns

| Pattern | Use Case | Complexity |
|---------|----------|------------|
| **One-liner** (`ts_backtest_auto`) | Quick evaluation, 80% of use cases | Simple |
| **Modular** (`ts_cv_split` + `ts_cv_forecast_by`) | Custom pipelines, regression models | Advanced |

---

## One-Liner Backtest

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
    7, 5, '1d', MAP{}
);

-- With STRUCT params (mixed types)
SELECT * FROM ts_backtest_auto_by(
    'sales_data', store_id, date, revenue, 7, 5, '1d',
    {'method': 'Naive', 'gap': 2, 'clip_horizon': true}
);

-- Different metric
SELECT * FROM ts_backtest_auto_by(
    'sales_data', store_id, date, revenue, 7, 5, '1d',
    MAP{'method': 'Theta'},
    NULL, 'smape'
);

-- Aggregate results
SELECT
    model_name,
    AVG(abs_error) AS mae,
    AVG(fold_metric_score) AS avg_rmse
FROM ts_backtest_auto_by('sales_data', store_id, date, revenue, 7, 5, '1d', MAP{})
GROUP BY model_name;
```

---

## Modular Functions

### ts_cv_generate_folds

Automatically generate fold boundaries based on data range.

**Signature:**
```sql
ts_cv_generate_folds(
    source VARCHAR,
    date_col VARCHAR,
    n_folds BIGINT,
    horizon BIGINT,
    frequency VARCHAR,
    params MAP
) → TABLE(training_end_times DATE[])
```

**Example:**
```sql
SELECT training_end_times
FROM ts_cv_generate_folds('sales_data', 'date', 3, 5, '1d', MAP{});
-- Returns: [2024-01-15, 2024-01-20, 2024-01-25]
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
    source VARCHAR,
    group_col VARCHAR,
    date_col VARCHAR,
    target_col VARCHAR,
    training_end_times DATE[],
    horizon BIGINT,
    frequency VARCHAR,
    params MAP
) → TABLE
```

**Returns:** Rows from source with `fold_id` and `split` (`'train'` or `'test'`) columns.

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

---

### ts_cv_split_index_by

Memory-efficient alternative returning only index columns.

**Signature:**
```sql
ts_cv_split_index_by(
    source VARCHAR,
    group_col VARCHAR,
    date_col VARCHAR,
    training_end_times DATE[],
    horizon BIGINT,
    frequency VARCHAR,
    params MAP
) → TABLE(group_col, date_col, fold_id BIGINT, split VARCHAR)
```

---

### ts_cv_forecast_by

Generate forecasts for all CV folds.

**Signature:**
```sql
ts_cv_forecast_by(
    cv_splits VARCHAR,
    group_col VARCHAR,
    date_col VARCHAR,
    target_col VARCHAR,
    method VARCHAR,
    horizon BIGINT,
    params MAP,
    frequency VARCHAR
) → TABLE
```

**Example:**
```sql
-- Create splits
CREATE TABLE cv_splits AS
SELECT * FROM ts_cv_split_by('sales', 'store_id', 'date', 'sales',
    ['2024-01-10'::DATE, '2024-01-15'::DATE], 5, '1d', MAP{});

-- Generate forecasts for training data only
SELECT * FROM ts_cv_forecast_by(
    'cv_splits',
    'store_id', 'date', 'sales',
    'AutoETS', 5, MAP{}, '1d'
) WHERE split = 'train';
```

---

## Typical Workflow

```sql
-- 1. Generate fold boundaries
SELECT training_end_times FROM ts_cv_generate_folds('data', 'date', 3, 5, '1d', MAP{});

-- 2. View fold date ranges
SELECT * FROM ts_cv_split_folds_by('data', 'group_id', 'date', [...], 5, '1d');

-- 3. Create train/test splits
SELECT * FROM ts_cv_split_by('data', 'group_id', 'date', 'value', [...], 5, '1d', MAP{});

-- Or use one-liner for quick evaluation
SELECT * FROM ts_backtest_auto_by('data', group_id, date, value, 5, 3, '1d', MAP{});
```

---

## Advanced: CV Data Hydration

> **Note:** These functions are for building custom cross-validation pipelines.
> For standard backtesting, use `ts_backtest_auto` which handles everything automatically.

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
    MAP{'strategy': 'last_value'}
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

**Output Columns:** Same as `ts_hydrate_split_full` including `_is_test` flag.

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
| `ts_hydrate_split` | Mask one specific column | Medium |
| `ts_hydrate_split_full` | Manual masking with `_is_test` flag | Medium |
| `ts_hydrate_split_strict` | Force explicit column handling | High |
| `ts_hydrate_features` | Auto-hydrate with masking flags | Medium |
| `ts_prepare_regression_input` | Regression model prep | High |

---

*See also: [Forecasting](05-forecasting.md) | [Evaluation Metrics](07-evaluation-metrics.md)*
