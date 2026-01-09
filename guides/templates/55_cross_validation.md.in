# Time Series Cross-Validation

## Introduction

Time series cross-validation (CV) is essential for evaluating forecasting models without data leakage. Unlike standard k-fold cross-validation used in machine learning, time series CV must respect the temporal ordering of data - the model can only be trained on past data to predict future values.

The anofox-forecast extension provides a complete set of functions for time series cross-validation:

**Key Functions**:

- `ts_cv_split` - Split data into train/test sets for multiple CV folds
- `ts_cv_split_folds` - Generate fold boundary information
- `ts_cv_generate_folds` - Automatically generate training end times
- `ts_mark_unknown` - Mark data points as known/unknown for scenario testing
- `ts_fill_unknown` - Fill future feature values in test periods
- `ts_validate_timestamps` - Validate expected timestamps exist in data

---

## Table of Contents

1. [Understanding Time Series Cross-Validation](#understanding-time-series-cross-validation)
2. [Quick Start Example](#quick-start-example)
3. [Window Types](#window-types)
4. [Function Reference](#function-reference)
   - [ts_cv_split](#ts_cv_split)
   - [ts_cv_split_folds](#ts_cv_split_folds)
   - [ts_cv_generate_folds](#ts_cv_generate_folds)
   - [ts_mark_unknown](#ts_mark_unknown)
   - [ts_fill_unknown](#ts_fill_unknown)
   - [ts_validate_timestamps](#ts_validate_timestamps)
5. [Handling Exogenous Variables](#handling-exogenous-variables)
6. [Best Practices](#best-practices)
7. [Complete Workflow Example](#complete-workflow-example)

---

## Understanding Time Series Cross-Validation

### Why Special Cross-Validation for Time Series?

Standard k-fold cross-validation randomly shuffles data, which causes **data leakage** in time series:

```
Standard CV (WRONG for time series):
Fold 1: Train=[Jan,Mar,Apr] Test=[Feb]  ← Future data (Mar,Apr) used to predict past (Feb)!
Fold 2: Train=[Jan,Feb,Apr] Test=[Mar]  ← Future data (Apr) used to predict past (Mar)!
```

Time series CV maintains temporal order:

```
Time Series CV (CORRECT):
Fold 1: Train=[Jan,Feb,Mar]     Test=[Apr]      ← Only past predicts future
Fold 2: Train=[Jan,Feb,Mar,Apr] Test=[May]      ← Training window expands
Fold 3: Train=[Jan,Feb,Mar,Apr,May] Test=[Jun]  ← Each fold adds more history
```

### The Rolling Origin Approach

Time series CV uses a "rolling origin" where:

1. The training set starts from the beginning of the data
2. The training end point (origin) advances through time
3. The test set is always immediately after training
4. The test horizon is fixed (e.g., always 7 days ahead)

```
Data: [==========================================================]
      Jan        Feb        Mar        Apr        May        Jun

Fold 1: [TRAIN TRAIN TRAIN TRAIN]  [TEST TEST]
Fold 2: [TRAIN TRAIN TRAIN TRAIN TRAIN TRAIN]  [TEST TEST]
Fold 3: [TRAIN TRAIN TRAIN TRAIN TRAIN TRAIN TRAIN TRAIN]  [TEST TEST]
```

[Go to top](#time-series-cross-validation)

---

## Quick Start Example

Here's a minimal example to get started with time series CV:

```sql
-- Create sample data: 2 series, 30 days each
CREATE TABLE cv_demo AS
SELECT
    series_id,
    '2024-01-01'::DATE + (i * INTERVAL '1 day') AS date,
    10.0 + series_id * 50 + i + 5 * SIN(2 * PI() * i / 7) AS value
FROM generate_series(0, 29) AS t(i)
CROSS JOIN (SELECT UNNEST(['A', 'B']) AS series_id) s;

-- Split into 2 folds with 7-day test horizon
SELECT
    group_col AS series_id,
    date_col AS date,
    target_col AS value,
    fold_id,
    split
FROM ts_cv_split(
    'cv_demo',
    series_id,
    date,
    value,
    ['2024-01-15'::DATE, '2024-01-22'::DATE],  -- Two fold cutoffs
    7,                                          -- 7-day horizon
    '1d'                                        -- Daily frequency
)
ORDER BY fold_id, series_id, date;
```

This produces train/test splits where:

- **Fold 1**: Train Jan 1-15, Test Jan 16-22
- **Fold 2**: Train Jan 1-22, Test Jan 23-29

[Go to top](#time-series-cross-validation)

---

## Window Types

`ts_cv_split` supports three window strategies via the `window_type` parameter:

### Expanding Window (Default)

Training set grows with each fold. All historical data is used.

```sql
-- Expanding window: training starts from data beginning
SELECT * FROM ts_cv_split(
    'cv_demo', series_id, date, value,
    ['2024-01-10'::DATE, '2024-01-17'::DATE],
    7, '1d',
    window_type := 'expanding'
);

-- Fold 1: Train Jan 1-10, Test Jan 11-17
-- Fold 2: Train Jan 1-17, Test Jan 18-24
--         ↑ Training window expands
```

**Use when**: You want to use all available history, which typically improves accuracy.

### Fixed Window

Training set has constant size. Oldest data is dropped as new data is added.

```sql
-- Fixed window: always use exactly 10 days of training
SELECT * FROM ts_cv_split(
    'cv_demo', series_id, date, value,
    ['2024-01-15'::DATE, '2024-01-22'::DATE],
    7, '1d',
    window_type := 'fixed',
    min_train_size := 10
);

-- Fold 1: Train Jan 6-15, Test Jan 16-22   (10 days)
-- Fold 2: Train Jan 13-22, Test Jan 23-29  (10 days)
--         ↑ Window slides, constant size
```

**Use when**: Old data becomes less relevant (concept drift) or training time is a concern.

### Sliding Window

Similar to fixed, but emphasizes recency. Window slides forward.

```sql
-- Sliding window with minimum training size
SELECT * FROM ts_cv_split(
    'cv_demo', series_id, date, value,
    ['2024-01-15'::DATE, '2024-01-22'::DATE],
    7, '1d',
    window_type := 'sliding',
    min_train_size := 7
);
```

**Use when**: Recent patterns matter most, or data characteristics change over time.

[Go to top](#time-series-cross-validation)

---

## Function Reference

### ts_cv_split

Splits time series data into train/test sets for cross-validation.

**Signature:**

```sql
ts_cv_split(
    source              VARCHAR,    -- Table name
    group_col           COLUMN,     -- Series identifier column
    date_col            COLUMN,     -- Date/timestamp column
    target_col          COLUMN,     -- Target value column
    training_end_times  DATE[],     -- List of training cutoff dates
    horizon             INTEGER,    -- Number of test periods
    frequency           VARCHAR,    -- Time frequency ('1d', '1h', '1w', '1mo')
    window_type         VARCHAR,    -- 'expanding' (default), 'fixed', or 'sliding'
    min_train_size      INTEGER     -- Minimum training periods (for fixed/sliding)
) → TABLE
```

**Output Columns:**

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | VARCHAR | Series identifier |
| `date_col` | TIMESTAMP | Date/timestamp |
| `target_col` | DOUBLE | Target value |
| `fold_id` | BIGINT | Fold number (1, 2, 3, ...) |
| `split` | VARCHAR | 'train' or 'test' |

**Example:**

```sql
-- 3-fold CV with weekly frequency
SELECT
    fold_id,
    split,
    COUNT(*) AS n_rows
FROM ts_cv_split(
    'weekly_sales',
    store_id,
    week_date,
    revenue,
    ['2024-03-01', '2024-04-01', '2024-05-01']::DATE[],
    4,      -- 4-week horizon
    '1w'    -- Weekly frequency
)
GROUP BY fold_id, split
ORDER BY fold_id, split;
```

[Go to top](#time-series-cross-validation)

---

### ts_cv_split_folds

Returns fold boundary information without the actual data split.

**Signature:**

```sql
ts_cv_split_folds(
    source              VARCHAR,
    group_col           COLUMN,
    date_col            COLUMN,
    training_end_times  DATE[],
    horizon             INTEGER,
    frequency           VARCHAR
) → TABLE
```

**Output Columns:**

| Column | Type | Description |
|--------|------|-------------|
| `fold_id` | BIGINT | Fold number |
| `train_start` | TIMESTAMP | Training period start |
| `train_end` | TIMESTAMP | Training period end (cutoff) |
| `test_start` | TIMESTAMP | Test period start |
| `test_end` | TIMESTAMP | Test period end |
| `horizon` | BIGINT | Test horizon |

**Example:**

```sql
-- Inspect fold boundaries before running full CV
SELECT * FROM ts_cv_split_folds(
    'sales_data',
    product_id,
    date,
    ['2024-02-01', '2024-03-01']::DATE[],
    14,
    '1d'
);
```

**Result:**

```
fold_id | train_start | train_end  | test_start | test_end   | horizon
--------+-------------+------------+------------+------------+---------
      1 | 2024-01-01  | 2024-02-01 | 2024-02-02 | 2024-02-15 |      14
      2 | 2024-01-01  | 2024-03-01 | 2024-03-02 | 2024-03-15 |      14
```

[Go to top](#time-series-cross-validation)

---

### ts_cv_generate_folds

Automatically generates training end times based on data range.

**Signature:**

```sql
ts_cv_generate_folds(
    source              VARCHAR,
    date_col            COLUMN,
    n_folds             INTEGER,    -- Desired number of folds
    horizon             INTEGER,    -- Test horizon per fold
    frequency           VARCHAR,    -- Time frequency
    initial_train_size  INTEGER     -- Optional: minimum initial training size
) → TABLE
```

**Output Columns:**

| Column | Type | Description |
|--------|------|-------------|
| `training_end_times` | TIMESTAMP[] | List of cutoff dates for each fold |

**Example:**

```sql
-- Auto-generate 5 folds with 7-day horizon
SELECT training_end_times
FROM ts_cv_generate_folds('daily_sales', date, 5, 7, '1d');

-- Use with ts_cv_split
WITH folds AS (
    SELECT training_end_times
    FROM ts_cv_generate_folds('daily_sales', date, 5, 7, '1d')
)
SELECT *
FROM ts_cv_split(
    'daily_sales',
    store_id,
    date,
    sales,
    (SELECT training_end_times FROM folds),
    7,
    '1d'
);
```

[Go to top](#time-series-cross-validation)

---

### ts_mark_unknown

Marks rows as known/unknown based on a cutoff date. Useful for scenario analysis and custom filling strategies.

**Signature:**

```sql
ts_mark_unknown(
    source              VARCHAR,
    group_col           COLUMN,
    date_col            COLUMN,
    cutoff_date         DATE
) → TABLE
```

**Output Columns:**

| Column | Type | Description |
|--------|------|-------------|
| (all source columns) | various | Original columns preserved |
| `is_unknown` | BOOLEAN | TRUE if date > cutoff |
| `last_known_date` | TIMESTAMP | Last known date per group |

**Example:**

```sql
-- Mark data points as known/unknown
SELECT
    product_id,
    date,
    price,
    is_unknown,
    last_known_date
FROM ts_mark_unknown(
    'product_prices',
    product_id,
    date,
    '2024-06-01'::DATE
)
WHERE date BETWEEN '2024-05-28' AND '2024-06-05';
```

**Result:**

```
product_id | date       | price  | is_unknown | last_known_date
-----------+------------+--------+------------+-----------------
A          | 2024-05-28 | 100.0  | false      | 2024-06-01
A          | 2024-05-29 | 101.0  | false      | 2024-06-01
A          | 2024-06-01 | 103.0  | false      | 2024-06-01
A          | 2024-06-02 | 104.0  | true       | 2024-06-01  ← Unknown future
A          | 2024-06-03 | 105.0  | true       | 2024-06-01
```

[Go to top](#time-series-cross-validation)

---

### ts_fill_unknown

Fills unknown (future) values in test periods with specified strategy.

**Signature:**

```sql
ts_fill_unknown(
    source              VARCHAR,
    group_col           COLUMN,
    date_col            COLUMN,
    value_col           COLUMN,
    cutoff_date         DATE,
    strategy            VARCHAR,    -- 'last_value' (default), 'default', or 'null'
    fill_value          DOUBLE      -- Value for 'default' strategy
) → TABLE
```

**Strategies:**

- `'last_value'` (default): Forward fill from last known value
- `'default'`: Fill with specified `fill_value`
- `'null'`: Leave as NULL

**Example:**

```sql
-- Fill unknown future values with last known value
SELECT *
FROM ts_fill_unknown(
    'feature_data',
    store_id,
    date,
    promotion_flag,
    '2024-06-01'::DATE,
    strategy := 'last_value'
);

-- Fill with default value (0)
SELECT *
FROM ts_fill_unknown(
    'feature_data',
    store_id,
    date,
    temperature,
    '2024-06-01'::DATE,
    strategy := 'default',
    fill_value := 20.0
);
```

[Go to top](#time-series-cross-validation)

---

### ts_validate_timestamps

Validates that expected timestamps exist in data for each group.

**Signature:**

```sql
ts_validate_timestamps(
    source              VARCHAR,
    group_col           COLUMN,
    date_col            COLUMN,
    expected_timestamps TIMESTAMP[]
) → TABLE
```

**Output Columns:**

| Column | Type | Description |
|--------|------|-------------|
| `group_col` | VARCHAR | Series identifier |
| `is_valid` | BOOLEAN | TRUE if all timestamps present |
| `n_expected` | BIGINT | Number of expected timestamps |
| `n_found` | BIGINT | Number found in data |
| `n_missing` | BIGINT | Number missing |
| `missing_timestamps` | TIMESTAMP[] | List of missing timestamps |

**Example:**

```sql
-- Validate data has all expected dates
SELECT *
FROM ts_validate_timestamps(
    'daily_sales',
    store_id,
    date,
    generate_series('2024-01-01'::DATE, '2024-01-31'::DATE, INTERVAL '1 day')
);
```

Also available: `ts_validate_timestamps_summary` for a quick overview across all groups.

[Go to top](#time-series-cross-validation)

---

## Handling Exogenous Variables

When your forecasting model uses exogenous (external) variables, special care is needed during cross-validation because future values of these features are unknown at prediction time.

### The Data Leakage Problem with Features

```
Training Period              | Test Period
[Jan 1] ... [Feb 28]        | [Mar 1] ... [Mar 7]
           ↓                            ↓
    Features known           Features UNKNOWN at prediction time!
```

If you use actual future feature values during testing, your evaluation will be optimistic (too good).

### Solution: Fill Unknown Feature Values

Use `ts_fill_unknown` to handle features realistically:

```sql
-- Original feature data with future values we wouldn't know
CREATE TABLE features AS
SELECT
    store_id,
    date,
    temperature,
    is_holiday,
    promotion_intensity
FROM raw_features;

-- For CV: fill unknown future features
CREATE TABLE cv_features AS
SELECT
    store_id,
    date,
    -- Temperature: forward fill (reasonable assumption)
    (SELECT value_col FROM ts_fill_unknown(
        'features', store_id, date, temperature,
        '2024-06-01'::DATE, strategy := 'last_value'
    ) WHERE features.store_id = store_id AND features.date = date) AS temperature,

    -- Holiday: use known calendar
    is_holiday,  -- Holidays are known in advance

    -- Promotion: assume no promotion if unknown
    (SELECT value_col FROM ts_fill_unknown(
        'features', store_id, date, promotion_intensity,
        '2024-06-01'::DATE, strategy := 'default', fill_value := 0.0
    ) WHERE features.store_id = store_id AND features.date = date) AS promotion_intensity
FROM features;
```

### Feature Categories

| Feature Type | Handling Strategy | Example |
|--------------|-------------------|---------|
| Known in advance | Use actual values | Holidays, day of week |
| Slowly changing | Forward fill | Temperature, price |
| Event-driven | Fill with default | Promotions, campaigns |
| Highly variable | Use forecast or NULL | Competitor actions |

[Go to top](#time-series-cross-validation)

---

## Best Practices

### 1. Avoid Data Leakage

- Never use future information in training data
- Be especially careful with features that are computed from future data
- Validate your CV setup with `ts_cv_split_folds` before running full evaluation

### 2. Choose Appropriate Horizon

- Match CV horizon to your actual prediction needs
- If you forecast 7 days ahead in production, use `horizon := 7` in CV
- Longer horizons typically show higher errors

### 3. Use Multiple Folds

- Single train/test split can be misleading
- 3-5 folds provides more robust estimates
- More folds = more compute time but better estimates

### 4. Match Production Conditions

```sql
-- If production uses expanding window, CV should too
SELECT * FROM ts_cv_split(
    'sales',
    store_id,
    date,
    revenue,
    training_end_times,
    horizon := 7,
    frequency := '1d',
    window_type := 'expanding'  -- Match production
);
```

### 5. Handle All Series Consistently

The CV functions automatically handle multiple series:

```sql
-- All series get the same fold structure
SELECT
    group_col AS store_id,
    fold_id,
    split,
    COUNT(*) AS n_rows
FROM ts_cv_split(...)
GROUP BY group_col, fold_id, split;
```

### 6. Validate Data Completeness

```sql
-- Check all series have expected timestamps
SELECT *
FROM ts_validate_timestamps_summary(
    'daily_sales',
    store_id,
    date,
    generate_series('2024-01-01'::DATE, '2024-06-30'::DATE, INTERVAL '1 day')
);
```

[Go to top](#time-series-cross-validation)

---

## Complete Workflow Example

This example demonstrates a complete CV workflow for evaluating multiple forecasting methods:

```sql
-- Step 1: Create sample data with multiple series
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
FROM generate_series(0, 119) AS t(d)      -- 120 days
CROSS JOIN generate_series(1, 3) AS s(s);  -- 3 stores

-- Step 2: Auto-generate fold cutoffs
CREATE TEMP TABLE fold_config AS
SELECT training_end_times
FROM ts_cv_generate_folds('sales_data', date, 3, 14, '1d', initial_train_size := 60);

-- Step 3: Split data into CV folds
CREATE TEMP TABLE cv_splits AS
SELECT *
FROM ts_cv_split(
    'sales_data',
    store_id,
    date,
    sales,
    (SELECT training_end_times FROM fold_config),
    14,
    '1d'
);

-- Step 4: View fold structure
SELECT
    fold_id,
    split,
    MIN(date_col) AS start_date,
    MAX(date_col) AS end_date,
    COUNT(*) / 3 AS days_per_series  -- Divide by 3 stores
FROM cv_splits
GROUP BY fold_id, split
ORDER BY fold_id, split DESC;

-- Step 5: Generate forecasts per fold (example with AutoETS)
CREATE TEMP TABLE cv_forecasts AS
SELECT
    cv.fold_id,
    cv.group_col AS store_id,
    cv.date_col AS date,
    f.point_forecast
FROM cv_splits cv
JOIN LATERAL (
    SELECT
        date AS forecast_date,
        point_forecast
    FROM ts_forecast_by(
        (SELECT * FROM cv_splits WHERE fold_id = cv.fold_id AND split = 'train'),
        group_col,
        date_col,
        target_col,
        'AutoETS',
        14,
        MAP{'seasonal_period': 7}
    )
) f ON cv.group_col = f.id AND cv.date_col = f.forecast_date
WHERE cv.split = 'test';

-- Step 6: Evaluate across folds
SELECT
    fold_id,
    store_id,
    ts_mae(LIST(target_col), LIST(point_forecast)) AS mae,
    ts_rmse(LIST(target_col), LIST(point_forecast)) AS rmse,
    ts_mape(LIST(target_col), LIST(point_forecast)) AS mape
FROM cv_splits cs
JOIN cv_forecasts cf USING (fold_id, store_id, date)
WHERE cs.split = 'test'
GROUP BY fold_id, store_id
ORDER BY fold_id, store_id;

-- Step 7: Aggregate performance across all folds
SELECT
    store_id,
    AVG(mae) AS avg_mae,
    AVG(rmse) AS avg_rmse,
    STDDEV(mae) AS std_mae  -- Stability across folds
FROM (
    SELECT
        fold_id,
        store_id,
        ts_mae(LIST(target_col), LIST(point_forecast)) AS mae,
        ts_rmse(LIST(target_col), LIST(point_forecast)) AS rmse
    FROM cv_splits cs
    JOIN cv_forecasts cf USING (fold_id, store_id, date)
    WHERE cs.split = 'test'
    GROUP BY fold_id, store_id
)
GROUP BY store_id
ORDER BY store_id;

-- Cleanup
DROP TABLE sales_data;
DROP TABLE fold_config;
DROP TABLE cv_splits;
DROP TABLE cv_forecasts;
```

This workflow:

1. Creates realistic time series data
2. Auto-generates appropriate CV folds
3. Splits data respecting temporal order
4. Generates forecasts for each fold's training data
5. Evaluates accuracy on each fold's test data
6. Aggregates results for robust performance estimates

[Go to top](#time-series-cross-validation)
