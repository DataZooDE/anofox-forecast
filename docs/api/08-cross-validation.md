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
SELECT * FROM ts_cv_folds_by('prepared_data', series_id, date, value, 3, 12, MAP{});
```

---

## Quick Start

### Cross-Validation Workflow (Recommended)

Use the two-step workflow for cross-validation:

```sql
-- Step 1: Generate folds (both train and test rows with actual dates)
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('data', unique_id, ds, y, 3, 12, MAP{});

-- Step 2: Generate forecasts (horizon inferred from test data)
CREATE TABLE cv_forecasts AS
SELECT * FROM ts_cv_forecast_by('cv_folds', unique_id, ds, y, 'Naive', MAP{});

-- Step 3: Compute metrics per series and fold
SELECT
    unique_id,
    fold_id,
    ts_rmse(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS rmse,
    ts_mae(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mae
FROM cv_forecasts
GROUP BY unique_id, fold_id;
```

### Usage Pattern Comparison

| Pattern | Use Case | Complexity |
|---------|----------|------------|
| **Two-step** (`ts_cv_folds_by` + `ts_cv_forecast_by`) | Standard backtesting, most use cases | Simple |
| **Custom cutoffs** (`ts_cv_split_by`) | Specific business dates as fold boundaries | Moderate |
| **With features** (`ts_cv_hydrate_by`) | Regression models, external forecasters | Moderate |

---

## Table Macros

### ts_cv_folds_by

Create train/test splits for ML model backtesting in a single function call.

This function combines fold boundary generation and train/test splitting, suitable for ML model backtesting. Unlike `ts_cv_split_by` which requires pre-computed training cutoff dates, this function automatically computes fold boundaries from the data.

**Signature:**
```sql
ts_cv_folds_by(
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
| `<target_col>` | DOUBLE | Target value (preserves original column name) |
| `fold_id` | BIGINT | Fold number (1, 2, 3, ...) |
| `split` | VARCHAR | `'train'` or `'test'` |

> **Note:** Output contains exactly 5 columns. Features are not passed through. Use `ts_cv_hydrate_by` to join features from the source table when needed.

**Examples:**
```sql
-- Basic ML backtesting with 3 folds, 6-period horizon
SELECT * FROM ts_cv_folds_by(
    'sales_data', store_id, date, revenue,
    3, 6, MAP{}
);

-- With gap between train and test (e.g., for "next week" predictions)
SELECT * FROM ts_cv_folds_by(
    'sales_data', store_id, date, revenue,
    3, 7, {'gap': 1}
);

-- Fixed window with custom initial training size
SELECT * FROM ts_cv_folds_by(
    'sales_data', store_id, date, revenue,
    5, 12, {'window_type': 'fixed', 'min_train_size': 24, 'initial_train_size': 24}
);

-- Dense overlapping folds (skip_length=1)
SELECT * FROM ts_cv_folds_by(
    'sales_data', store_id, date, revenue,
    10, 3, {'skip_length': 1}
);
```

---

### ts_cv_forecast_by

Generate **univariate** forecasts for pre-computed cross-validation folds. **Use this with output from `ts_cv_folds_by`.**

> **Univariate only:** This function forecasts using only the target column history. It does not support exogenous variables or feature columns. For models with features, use `ts_cv_hydrate_by` with your own regression model.

**Signature:**
```sql
ts_cv_forecast_by(
    ml_folds VARCHAR,         -- Output from ts_cv_folds_by or ts_cv_split_by
    group_col COLUMN,         -- Group column
    date_col COLUMN,          -- Date column
    target_col COLUMN,        -- Target column (original name preserved)
    method VARCHAR,           -- Forecast method (e.g., 'Naive', 'AutoETS', 'AutoARIMA')
    params MAP = {}           -- Model parameters
) → TABLE
```

> **Note:** Horizon is automatically inferred from the number of test rows per fold/group. No need to specify it separately.

**Input Requirements:**

The input table must be the output of `ts_cv_folds_by` (or `ts_cv_split_by`) containing **both train and test** rows:
- `fold_id`: Fold identifier
- `split`: 'train' or 'test'
- Group, date, and target columns (original names preserved)

**Output:**

Returns the test rows with forecast columns added:
| Column | Type | Description |
|--------|------|-------------|
| `fold_id` | BIGINT | Fold number |
| `<group_col>` | (same as input) | Series identifier |
| `<date_col>` | (same as input) | Forecast date |
| `<target_col>` | DOUBLE | Actual value from test data (original name preserved) |
| `split` | VARCHAR | Always 'test' |
| `forecast` | DOUBLE | Point forecast |
| `lower_90` | DOUBLE | Lower 90% prediction interval |
| `upper_90` | DOUBLE | Upper 90% prediction interval |
| `model_name` | VARCHAR | Model used |

**Key Features:**

- **No frequency parameter needed**: Forecasts are matched to existing test dates from input
- **Date preservation**: Original date values are preserved (no date generation)
- **Position-based matching**: 1st forecast → 1st test row, 2nd forecast → 2nd test row, etc.


**Example:**

```sql
-- Step 1: Create folds (both train and test rows)
CREATE TABLE folds AS
SELECT * FROM ts_cv_folds_by('data', unique_id, ds, y, 3, 6, MAP{});

-- Step 2: Generate forecasts (horizon inferred from test data)
CREATE TABLE cv_results AS
SELECT * FROM ts_cv_forecast_by('folds', unique_id, ds, y, 'Naive', MAP{});

-- Step 3: Compute metrics per series and fold
SELECT
    unique_id,
    fold_id,
    ts_rmse(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS rmse,
    ts_mae(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mae,
    ts_mape(LIST(y ORDER BY ds), LIST(forecast ORDER BY ds)) AS mape
FROM cv_results
GROUP BY unique_id, fold_id;
```

---

## Regression & ML Model Backtesting

> Use `ts_cv_hydrate_by` to add feature columns for regression models or external forecasters.

**When to use this function:**
- You need to add feature columns from your source table to CV folds
- You want to mask unknown features in test rows to prevent data leakage
- You're using regression models that need exogenous variables

### ts_cv_hydrate_by

Hydrate CV folds with unknown features from a source table. **Masking is applied automatically** - train rows get actual values, test rows get filled values per strategy. **Unknown features are output as direct columns** (not a MAP), making them immediately usable without extraction.

**Signature:**
```sql
ts_cv_hydrate_by(
    cv_folds VARCHAR,           -- Output from ts_cv_folds_by
    source VARCHAR,             -- Source table containing unknown features
    group_col COLUMN,           -- Group column (same in both tables)
    date_col COLUMN,            -- Date column (same in both tables)
    unknown_features VARCHAR[], -- Column names to hydrate with masking
    params MAP = {}             -- Fill strategy options
) → TABLE
```

**Params Options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `strategy` | VARCHAR | `'last_value'` | `'last_value'`, `'null'`, or `'default'` |
| `fill_value` | VARCHAR | `''` | Value for `'default'` strategy |

**Fill Strategies (for test rows):**
| Strategy | Train Rows | Test Rows |
|----------|------------|-----------|
| `'last_value'` (default) | Actual values | Last training value per group/fold |
| `'null'` | Actual values | NULL |
| `'default'` | Actual values | Specified `fill_value` |

**Output Columns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `<date_col>` | (same as input) | Date/timestamp |
| `<target_col>` | DOUBLE | Target value |
| `fold_id` | BIGINT | Fold number |
| `split` | VARCHAR | `'train'` or `'test'` |
| `<feature_1>` | VARCHAR | First unknown feature (with masking) |
| `<feature_2>` | VARCHAR | Second unknown feature (with masking) |
| ... | VARCHAR | Additional unknown features |

**Basic Usage:**

```sql
-- Create folds
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('sales', store_id, date, revenue, 3, 30, MAP{});

-- Hydrate with unknown features (masking applied automatically)
-- Unknown features are direct columns - no MAP extraction needed!
SELECT
    store_id, date, revenue, fold_id, split,
    competitor_sales,  -- Direct column access
    actual_temp        -- Direct column access
FROM ts_cv_hydrate_by(
    'cv_folds', 'sales', store_id, date,
    ['competitor_sales', 'actual_temp'],
    MAP{'strategy': 'last_value'}
);
```

The unknown feature columns contain:
- **Train rows**: Actual values from source
- **Test rows**: Filled values per strategy (no data leakage)

### The Data Leakage Problem

In time series CV, the test set represents "future" data. Features that depend on future values must be masked to prevent leakage:

```
Timeline:    [====== TRAIN ======][=== TEST ===]
                                  ↑
                            cutoff date

Known features:     ✓ Use directly (day_of_week, is_holiday)
Unknown features:   ✓ Train: actual values
                    ✗ Test: must be masked → use ts_cv_hydrate_by
```

### ML Regression Workflow

```sql
-- Step 1: Create folds
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('sales', store_id, date, revenue, 3, 30, MAP{});

-- Step 2: Build ML-ready dataset (unknown features already masked)
-- Unknown features are direct columns - no extraction needed!
CREATE TABLE ml_input AS
SELECT
    store_id, date, fold_id, split,
    -- Target: NULL for test rows (to predict)
    CASE WHEN split = 'test' THEN NULL ELSE revenue END AS target,
    -- Unknown features: automatically masked (actual for train, filled for test)
    competitor_sales::DOUBLE AS competitor_sales,
    actual_temp::DOUBLE AS actual_temp
FROM ts_cv_hydrate_by(
    'cv_folds', 'sales', store_id, date,
    ['competitor_sales', 'actual_temp'],
    MAP{'strategy': 'last_value'}
);

-- Optional: join with source for additional known features
-- SELECT ml.*, s.day_of_week, s.is_holiday
-- FROM ml_input ml JOIN sales s USING (store_id, date);

-- Step 3: Train on rows WHERE target IS NOT NULL
-- Step 4: Predict on rows WHERE target IS NULL
```

---

## Advanced: Custom Fold Boundaries

> Use these functions when you need precise control over fold cutoff dates (fiscal calendars, business events, regulatory requirements).

For most standard backtesting, prefer `ts_cv_folds_by` which automatically computes fold boundaries.

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

*See also: [Forecasting](07-forecasting.md) | [Evaluation Metrics](09-evaluation-metrics.md)*
