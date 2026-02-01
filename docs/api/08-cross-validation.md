# Cross-Validation & Backtesting

> Time series cross-validation with proper temporal ordering

## Overview

Time series cross-validation requires special handling because data has temporal ordering. These functions help you create proper train/test splits, handle unknown features during backtesting, and prevent data leakage.

**Use this document to:**
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

-- Then generate folds on prepared data
SELECT * FROM ts_ml_folds_by('prepared_data', series_id, date, value, 3, 12, MAP{});
```

---

## Quick Start

### Cross-Validation Workflow (Recommended)

Use the two-step workflow for cross-validation:

```sql
-- Step 1: Generate folds (both train and test rows with actual dates)
CREATE TABLE cv_folds AS
SELECT * FROM ts_ml_folds_by('data', unique_id, ds, y, 3, 12, MAP{});

-- Step 2: Generate forecasts (matches to existing test dates)
CREATE TABLE cv_forecasts AS
SELECT * FROM ts_cv_forecast_by('cv_folds', unique_id, ds, y, 'Naive', 12, MAP{});

-- Step 3: Compute metrics per fold
SELECT * FROM ts_rmse_by('cv_forecasts', fold_id, ds, y, forecast);
SELECT * FROM ts_mae_by('cv_forecasts', fold_id, ds, y, forecast);
```

**Key insight:** `ts_ml_folds_by` outputs both train AND test rows with their actual dates, so `ts_cv_forecast_by` doesn't need to generate dates.

### Usage Pattern Comparison

| Pattern | Use Case | Complexity |
|---------|----------|------------|
| **Two-step** (`ts_ml_folds_by` + `ts_cv_forecast_by`) | Standard backtesting, most use cases | Simple |
| **Custom cutoffs** (`ts_cv_split_by`) | Specific business dates as fold boundaries | Moderate |
| **Modular** (`ts_cv_split_by` + custom models) | Regression models, external forecasters | Advanced |

---

## Table Macros

### ts_ml_folds_by

Create train/test splits for ML model backtesting in a single function call.

This function combines fold boundary generation and train/test splitting, suitable for ML model backtesting. Unlike using `ts_cv_generate_folds` + `ts_cv_split_by` separately, this function avoids DuckDB's subquery limitation and provides a simpler API.

**Signature:**
```sql
ts_ml_folds_by(
    source VARCHAR,           -- Source table name (quoted string, NOT a CTE reference)
    group_col COLUMN,         -- Series grouping column (unquoted identifier)
    date_col COLUMN,          -- Date column (unquoted identifier)
    target_col COLUMN,        -- Value column (unquoted identifier)
    n_folds BIGINT,           -- Number of folds to generate
    horizon BIGINT,           -- Number of periods in test window
    params MAP or STRUCT      -- Optional parameters (see below)
) → TABLE
```

> **Important:**
> - **Source must be a table name as a string** (e.g., `'my_table'`), not a CTE reference. CTEs are not supported.
> - Assumes pre-cleaned data with no gaps. Use `ts_fill_gaps_by` first if your data has missing dates.
> - Uses position-based indexing (not date arithmetic) - works correctly with all frequencies.
> - **Parameter validation:** Unknown parameter names will throw an informative error listing all available parameters.

**Params Options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `gap` | BIGINT | `0` | Periods between train end and test start |
| `embargo` | BIGINT | `0` | Periods to exclude from training after previous test |
| `window_type` | VARCHAR | `'expanding'` | `'expanding'`, `'fixed'`, or `'sliding'` |
| `min_train_size` | BIGINT | `1` | Minimum training size for fixed/sliding windows only (ignored for expanding) |
| `initial_train_size` | BIGINT | auto | Periods before first fold (default: n_dates - n_folds * horizon) |
| `skip_length` | BIGINT | `horizon` | Periods between folds (1=dense, horizon=default) |
| `clip_horizon` | BOOLEAN | `false` | If true, allow folds with partial test windows |

> **Note on `min_train_size`:** This parameter only affects `fixed` and `sliding` window types. For the default `expanding` window, training always starts from position 0. If you need a minimum training size with expanding windows, use `initial_train_size` instead.

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier (preserves original column name) |
| `<date_col>` | (same as input) | Date/timestamp (preserves original column name and type) |
| `y` | DOUBLE | Target value |
| `fold_id` | BIGINT | Fold number (1, 2, 3, ...) |
| `split` | VARCHAR | `'train'` or `'test'` |
| `<feature_cols...>` | (same as input) | All other columns from source table |

> **Feature Pass-Through:** All columns in the source table (except group, date, and target) are automatically passed through as features. Column names and types are preserved. Features are correctly associated with each row through the fold assignment process.

**Examples:**
```sql
-- Basic ML backtesting with 3 folds, 6-period horizon
SELECT * FROM ts_ml_folds_by(
    'sales_data', store_id, date, revenue,
    3, 6, MAP{}
);

-- With gap between train and test (e.g., for "next week" predictions)
SELECT * FROM ts_ml_folds_by(
    'sales_data', store_id, date, revenue,
    3, 7, {'gap': 1}
);

-- Fixed window with custom initial training size
SELECT * FROM ts_ml_folds_by(
    'sales_data', store_id, date, revenue,
    5, 12, {'window_type': 'fixed', 'min_train_size': 24, 'initial_train_size': 24}
);

-- Dense overlapping folds (skip_length=1)
SELECT * FROM ts_ml_folds_by(
    'sales_data', store_id, date, revenue,
    10, 3, {'skip_length': 1}
);

-- With feature columns (all non-group/date/target columns pass through)
CREATE TABLE sales_features AS
SELECT store_id, date, revenue, temperature, promo_flag, day_of_week
FROM sales_data;

SELECT * FROM ts_ml_folds_by(
    'sales_features', store_id, date, revenue,
    3, 6, MAP{}
);
-- Output: store_id, date, y, fold_id, split, temperature, promo_flag, day_of_week
```

---

### ts_cv_forecast_by

Generate forecasts for pre-computed cross-validation folds. **Use this with output from `ts_ml_folds_by`.**

**Signature:**
```sql
ts_cv_forecast_by(
    ml_folds VARCHAR,         -- Output from ts_ml_folds_by
    group_col COLUMN,         -- Group column
    date_col COLUMN,          -- Date column
    target_col COLUMN,        -- Target column (usually 'y' from ts_ml_folds_by)
    method VARCHAR,           -- Forecast method
    horizon BIGINT,           -- Forecast horizon
    params MAP = {}           -- Model parameters
) → TABLE
```

**Input Requirements:**

The input table must be the output of `ts_ml_folds_by` containing:
- `fold_id`: Fold identifier
- `split`: 'train' or 'test'
- Group, date, and target columns

**Output:**

Returns the test rows with forecast columns added:
| Column | Type | Description |
|--------|------|-------------|
| `fold_id` | BIGINT | Fold number |
| `<group_col>` | (same as input) | Series identifier |
| `<date_col>` | (same as input) | Forecast date |
| `y` | DOUBLE | Actual value from test data |
| `split` | VARCHAR | Always 'test' |
| `forecast` | DOUBLE | Point forecast |
| `lower_90` | DOUBLE | Lower 90% prediction interval |
| `upper_90` | DOUBLE | Upper 90% prediction interval |
| `model_name` | VARCHAR | Model used |

**Key Features:**

- **No frequency parameter needed**: Forecasts are matched to existing test dates from input
- **Date preservation**: Original date values are preserved (no date generation)
- **Position-based matching**: 1st forecast → 1st test row, 2nd forecast → 2nd test row, etc.

> **Note on Features:** `ts_cv_forecast_by` uses only the target column (`y`) for univariate forecasting. If your input from `ts_ml_folds_by` includes feature columns, they are ignored by the forecaster but preserved in the output. For regression models that use features, use `ts_prepare_regression_input_by` instead.

**Example:**

```sql
-- Step 1: Create folds
CREATE TABLE folds AS
SELECT * FROM ts_ml_folds_by('data', unique_id, ds, y, 3, 6, MAP{});

-- Step 2: Generate forecasts
CREATE TABLE cv_results AS
SELECT * FROM ts_cv_forecast_by('folds', unique_id, ds, y, 'Naive', 6, MAP{});

-- Step 3: Compute metrics per fold
SELECT * FROM ts_rmse_by('cv_results', fold_id, ds, y, forecast);
SELECT * FROM ts_mae_by('cv_results', fold_id, ds, y, forecast);
SELECT * FROM ts_mape_by('cv_results', fold_id, ds, y, forecast);
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

View fold date ranges (train/test boundaries) for pre-specified training cutoff dates.

**Use Case:** When you need to inspect or visualize the actual date boundaries for each fold before running the full split. Useful for:
- Verifying fold boundaries align with business periods (quarters, fiscal years)
- Documentation and reporting
- Debugging cross-validation setups

**Signature:**
```sql
ts_cv_split_folds_by(
    source VARCHAR,
    group_col VARCHAR,
    date_col VARCHAR,
    training_end_times DATE[],
    horizon BIGINT,
    frequency VARCHAR          -- Required for date arithmetic ('1d', '1mo', etc.)
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

**Example:**
```sql
-- View boundaries for quarterly backtests
SELECT * FROM ts_cv_split_folds_by(
    'sales_data', 'store_id', 'date',
    ['2024-03-31'::DATE, '2024-06-30'::DATE, '2024-09-30'::DATE],
    30, '1d'
);
```

---

### ts_cv_split_by

Split time series data into train/test sets using explicit training cutoff dates.

**Use Case:** When you need **precise control over fold boundaries** rather than automatic fold generation. Common scenarios:
- **Fiscal calendar alignment**: Folds must end at quarter/year boundaries
- **Business events**: Train up to a specific known event date
- **Regulatory requirements**: Auditable, reproducible fold boundaries
- **Custom spacing**: Non-uniform spacing between folds (e.g., monthly for recent, quarterly for historical)

For most standard backtesting, prefer `ts_ml_folds_by` which automatically computes fold boundaries.

**Signature:**
```sql
ts_cv_split_by(
    source VARCHAR,           -- Source table name (quoted string)
    group_col IDENTIFIER,     -- Series grouping column (unquoted)
    date_col IDENTIFIER,      -- Date/timestamp column (unquoted)
    target_col IDENTIFIER,    -- Value column (unquoted)
    training_end_times DATE[],-- Explicit cutoff dates for each fold
    horizon BIGINT,
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
-- Quarterly backtests with explicit fiscal quarter end dates
CREATE TABLE cv_splits AS
SELECT * FROM ts_cv_split_by(
    'sales', store_id, date, revenue,
    ['2024-03-31'::DATE, '2024-06-30'::DATE, '2024-09-30'::DATE],
    30, MAP{}
);
-- Output columns: store_id, date, revenue, fold_id, split
```

---

### ts_cv_split_index_by

Memory-efficient alternative returning only index columns (no target values).

**Use Case:** For large datasets where duplicating target values across folds would consume too much memory. Use with hydration functions to join back to source data.

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

## Advanced: CV Data Hydration

> These functions are for building custom cross-validation pipelines with regression models or external forecasters.

When building custom CV pipelines, you need to join CV splits back to your source data. These functions prevent **data leakage** by automatically masking features that wouldn't be known at prediction time.

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
