# Data Preparation

> Filtering, cleaning, and imputation table macros for time series

## Overview

Data preparation functions help clean and transform time series data before forecasting. These are **table macros** that operate on source tables and return transformed tables.

**Use this document to:**
- Fill gaps in irregular time series to ensure regular intervals
- Impute NULL values using a constant, forward-fill (LOCF), backward-fill (NOCB), or the series
  mean (there is no SQL-level linear-interpolation function; linear interpolation is used
  internally only for historical exogenous regressors and the panel forecasting grid — see
  [Edge NULLs](#edge-nulls-d-14-g26))
- Drop problematic series (constant, too short, too many NaNs/nulls)
- Differencing to remove trends before modeling
- Build data cleaning pipelines for production forecasting

---

## Table of Contents

- [Quick Start](#quick-start)
- [Series Filtering](#series-filtering) — `ts_drop_constant_by`, `ts_drop_short_by`, `ts_drop_gappy_by`, `ts_drop_zeros_by`
- [Edge Cleaning](#edge-cleaning) — `ts_drop_leading_zeros_by`, `ts_drop_trailing_zeros_by`, `ts_drop_edge_zeros_by`
- [Missing Value Imputation](#missing-value-imputation) — `ts_fill_nulls_const_by`, `ts_fill_nulls_forward_by`, `ts_fill_nulls_backward_by`, `ts_fill_nulls_mean_by`, [Edge NULLs](#edge-nulls-d-14-g26)
- [Gap Filling](#gap-filling) — `ts_fill_gaps_by`, `ts_fill_forward_by`
- [Differencing](#differencing) — `ts_diff_by`
- [Advanced: Native Gap Filling](#advanced-native-gap-filling) — Supported frequency formats
- [Advanced: Future Value Handling](#advanced-future-value-handling) — `ts_fill_unknown_by`, `ts_mark_unknown_by`
- [Advanced: Timestamp Validation](#advanced-timestamp-validation) — `ts_validate_timestamps_by`, `ts_validate_timestamps_summary_by`

---

## Quick Start

Common data cleaning pipeline (order matters for intermittent data):

```sql
-- Setup: raw_data(product_id, date, value), 2 groups x 130 daily rows with gaps
-- (every 11th day dropped) for this walkthrough.
CREATE OR REPLACE TABLE raw_data AS
SELECT product_id, date, value
FROM (
    SELECT
        product_id, i,
        (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
        50.0 + product_id * 5.0 + 0.4 * i + 6.0 * sin(2 * pi() * i / 7.0) AS value
    FROM range(0, 130) t(i)
    CROSS JOIN (SELECT unnest([1, 2]) AS product_id)
)
WHERE i % 11 != 0;
```

```sql
-- Step 1: Fill gaps in time series (ensures regular intervals)
CREATE TABLE gaps_filled AS
SELECT * FROM ts_fill_gaps_by('raw_data', product_id, date, value, '1d');

-- Step 2: Impute NULLs with 0.0 (preserves intermittent demand patterns)
CREATE TABLE nulls_filled AS
SELECT * FROM ts_fill_nulls_const_by('gaps_filled', product_id, date, value, 0.0);

-- Step 3: Drop short series (after filling, so intermittent series aren't lost)
CREATE TABLE clean_data AS
SELECT * FROM ts_drop_short_by('nulls_filled', product_id, 20);
```

!!! warning "Do not chain table functions in CTEs"
    Chaining multiple `_by` table functions via CTEs (e.g., `ts_fill_gaps_by` → `ts_fill_forward_by`) can return **0 rows** under parallel execution. This is a DuckDB engine limitation where the FinalExecute output of one table-in-out operator is not visible to a downstream table-in-out operator within the same query.

    **Always materialize each step as a separate table:**

```sql
-- Step 1: Fill gaps
CREATE TABLE gaps_filled AS
SELECT * FROM ts_fill_gaps_by('raw_data', product_id, date, value, '1d');

-- Step 2: Fill nulls (reads from materialized table)
CREATE TABLE nulls_filled AS
SELECT * FROM ts_fill_nulls_const_by('gaps_filled', product_id, date, value, 0.0);

-- Step 3: Filter short series
CREATE TABLE clean_data AS
SELECT * FROM ts_drop_short_by('nulls_filled', product_id, 20);
```

The following CTE pattern is **broken** and will silently return 0 rows with large datasets:

```sql
-- ❌ BROKEN: CTE chaining of table functions returns 0 rows
WITH gaps_filled AS (
    SELECT * FROM ts_fill_gaps_by('raw_data', product_id, date, value, '1d')
),
nulls_filled AS (
    SELECT * FROM ts_fill_nulls_const_by('gaps_filled', product_id, date, value, 0.0)
)
SELECT * FROM ts_drop_short_by('nulls_filled', product_id, 20);
```

---

## Series Filtering

### ts_drop_constant_by

Filters out series that classify as constant under the Rust `is_constant` check: fewer than
two present (non-NULL, non-NaN) values (**including an all-NULL group** — a breaking change
from the old `MIN(value)=MAX(value)` SQL rule, which kept all-NULL groups), or all present
values within a **scale-relative** tolerance of each other (`max - min <= 1e-10 *
max(1, max|x|)`, Phase 15 D-13, G25 — previously an absolute `f64::EPSILON` tolerance, under
which `[1e12, 1e12 + 0.001]` was NOT constant even though a 0.001 wiggle on a 1e12-scale
series is immaterial).

**Signature:**
```sql
ts_drop_constant_by(source VARCHAR, group_col COLUMN, value_col COLUMN) → TABLE
```

**Example:**
```sql
-- Remove constant series from sales data
SELECT * FROM ts_drop_constant_by('sales', product_id, quantity);
```

---

### ts_drop_short_by

Filters out series shorter than the minimum length.

**Signature:**
```sql
ts_drop_short_by(source VARCHAR, group_col COLUMN, min_length INTEGER) → TABLE
```

**Example:**
```sql
-- Keep only series with at least 20 observations
SELECT * FROM ts_drop_short_by('sales', product_id, 20);
```

---

### ts_drop_gappy_by

Filters out series with too many gaps.

**Signature:**
```sql
ts_drop_gappy_by(source VARCHAR, group_col COLUMN, value_col COLUMN, max_gap_ratio DOUBLE) → TABLE
```

**Example:**
```sql
-- Remove series where gaps exceed 10% of data
SELECT * FROM ts_drop_gappy_by('sales', product_id, quantity, 0.1);
```

---

### ts_drop_zeros_by

Filters out series that are all zeros.

**Signature:**
```sql
ts_drop_zeros_by(source VARCHAR, group_col COLUMN, value_col COLUMN) → TABLE
```

---

## Edge Cleaning

### ts_drop_leading_zeros_by

Removes leading zeros from each series.

**Signature:**
```sql
ts_drop_leading_zeros_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

**Example:**
```sql
SELECT * FROM ts_drop_leading_zeros_by('sales', product_id, date, quantity);
```

---

### ts_drop_trailing_zeros_by

Removes trailing zeros from each series.

**Signature:**
```sql
ts_drop_trailing_zeros_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

---

### ts_drop_edge_zeros_by

Removes both leading and trailing zeros from each series.

**Signature:**
```sql
ts_drop_edge_zeros_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

---

## Missing Value Imputation

### ts_fill_nulls_const_by

Replaces NULL values with a constant.

**Signature:**
```sql
ts_fill_nulls_const_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, fill_value DOUBLE) → TABLE
```

**Example:**
```sql
-- Fill missing values with 0
SELECT * FROM ts_fill_nulls_const_by('sales', product_id, date, quantity, 0.0);
```

---

### ts_fill_nulls_forward_by

Forward fills NULL values (last observation carried forward, LOCF).

**Signature:**
```sql
ts_fill_nulls_forward_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

**Example:**
```sql
SELECT * FROM ts_fill_nulls_forward_by('sales', product_id, date, quantity);
```

---

### ts_fill_nulls_backward_by

Backward fills NULL values (next observation carried backward, NOCB).

**Signature:**
```sql
ts_fill_nulls_backward_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

---

### Edge NULLs (D-14, G26)

`ts_fill_nulls_forward_by`/`ts_fill_nulls_backward_by` are **one-directional** — this is
documented behaviour, not a bug, and it is unchanged by Phase 15:

- `ts_fill_nulls_forward_by` (LOCF) leaves a **leading** NULL run as NULL (there is no prior
  value to carry forward) and fills every other NULL with the last observed value.
- `ts_fill_nulls_backward_by` (NOCB) mirrors it: a **trailing** NULL run stays NULL, every
  other NULL is filled with the next observed value.

```sql
-- [NULL, 1, NULL, 3, NULL] -> forward: [NULL, 1, 1, 3, 3]; backward: [1, 1, 3, 3, NULL]
```

**There is no SQL linear-interpolation function.** Where the extension interpolates
internally — historical exogenous regressors passed to `ts_forecast_exog_by`/`_ts_forecast_exog`,
and the panel forecasting grid — interior gaps are filled by LINEAR interpolation, but
**leading/trailing gaps are extended with the nearest observed value** (constant edge
extension, never extrapolated past the first/last observed point). A regressor whose history
has a NULL at either edge gets exactly the same forecast as the same regressor with that edge
replaced by the nearest observed value.

---

### ts_fill_nulls_mean_by

Fills NULL values with the series mean, computed by the Rust `fill_nulls_mean` (Phase 12
RUST-05). `NaN` counts as missing (Phase 10 NaN = NULL convention), same as NULL: it is excluded
from the mean and replaced by it. A group with no non-missing values at all keeps every value
NULL (nothing to average). All source columns are preserved.

**Signature:**
```sql
ts_fill_nulls_mean_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

---

## Gap Filling

### ts_fill_gaps_by

Fills gaps in time series by inserting rows for missing timestamps. Missing values are filled with NULL.

> **Performance:** Uses streaming implementation for 15x memory reduction on large datasets (e.g., 12 MB vs 181 MB for 1M rows).

**Signature:**
```sql
ts_fill_gaps_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, frequency VARCHAR) → TABLE
```

**Parameters:**
- `source`: Table name as string
- `group_col`: Column identifying each time series
- `date_col`: Timestamp/date column
- `value_col`: Value column
- `frequency`: Time frequency string (e.g., `'1d'`, `'1h'`, `'1w'`, `'1 day'`)

**Example:**
```sql
-- Fill daily gaps
SELECT * FROM ts_fill_gaps_by('sales', product_id, date, quantity, '1d');
```

**Output:** Returns rows for all dates in range with NULL for missing values.

**Month-end / day-of-month rule (v2026.10.16, D-09):** for monthly/quarterly/yearly
frequencies, inserted dates step from the PREVIOUS observed date using the same
calendar rule `ts_forecast_by`'s future dates use: a month-end series stays
month-end, a 1st-of-month series stays on the 1st, and any other day-of-month keeps
its own day (clamped to the target month's length). Before v2026.10.16, calendar gap
fill always snapped inserted dates to the start of the period (the 1st).

```sql
-- Month-end series: the gap is filled on 2024-02-29 (month-end), not 2024-02-01
CREATE TABLE month_end_gaps AS
SELECT * FROM (VALUES
    (1, DATE '2024-01-31', 10.0),
    (1, DATE '2024-03-31', 30.0)
) t(product_id, date, quantity);

SELECT * FROM ts_fill_gaps_by('month_end_gaps', product_id, date, quantity, '1mo');
-- product_id | date       | quantity
-- 1          | 2024-01-31 | 10.0
-- 1          | 2024-02-29 | NULL
-- 1          | 2024-03-31 | 30.0
```

---

### ts_fill_forward_by

Extends time series forward to a target date, filling with NULL values.

> **Performance:** Uses streaming implementation for 10x memory reduction on large datasets.

**Signature:**
```sql
ts_fill_forward_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, target_date DATE, frequency VARCHAR) → TABLE
```

**Parameters:**
- `source`: Table name as string
- `group_col`: Column identifying each time series
- `date_col`: Timestamp/date column
- `value_col`: Value column
- `target_date`: Date to extend series to
- `frequency`: Time frequency string

**Example:**
```sql
-- Extend all series to end of year
SELECT * FROM ts_fill_forward_by('sales', product_id, date, quantity, '2024-12-31', '1d');
```

For monthly/quarterly/yearly `frequency`, `ts_fill_forward_by` steps forward from each
series' LAST observed date using the same month-end-preserving rule as `ts_fill_gaps_by`
above (D-09) — see that section for the exact behaviour and an example.

---

## Differencing

### ts_diff_by

Computes the d-th order difference of each series in Rust (Phase 12 RUST-03, D-05/D-06):
equivalent to numpy's `diff(x, n=diff_order)`, or R's `diff(x, lag=seasonal_lag,
differences=diff_order)` when `seasonal_lag` is given. Output has one row per input row; the
first `diff_order * seasonal_lag` rows of each group are NULL so dates stay aligned (D-06). Any
difference that touches a NULL/NaN input anywhere along the way is NULL — never an error (this
is the row-wise SQL policy, distinct from the single-series interior-NULL error rule used by
diagnostics functions).

**Signature:**
```sql
ts_diff_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, diff_order INTEGER, seasonal_lag := 1) → TABLE
```

**Parameters:**
- `diff_order`: Number of times the difference is applied (d-th order). `diff_order=2` gives the
  true 2nd-order difference `y[t] - 2*y[t-1] + y[t-2]` (numpy `diff(n=2)`), not a lag-2 difference.
- `seasonal_lag` (named, default `1`): the lag `k` used at each pass, `y[t] - y[t-k]`. Set
  `seasonal_lag := 2` to recover the lag-k behaviour `ts_diff_by` used before Phase 12
  (`diff_order=2` meant `y[t] - y[t-2]`, not true 2nd-order differencing).

> **Breaking change (D-05):** Before Phase 12, `diff_order` meant a lag-k difference
> (`y[t] - y[t-k]`), not iterative d-th order differencing. If you depended on the old lag
> behaviour, pass the same value as `seasonal_lag` instead and set `diff_order := 1`.

**Example:**
```sql
-- First-order difference: y[t] - y[t-1]
SELECT * FROM ts_diff_by('sales', product_id, date, quantity, 1);

-- True second-order difference: y[t] - 2*y[t-1] + y[t-2] (numpy diff(n=2))
SELECT * FROM ts_diff_by('sales', product_id, date, quantity, 2);

-- Old lag-2 reading (y[t] - y[t-2]): use seasonal_lag instead of diff_order=2
SELECT * FROM ts_diff_by('sales', product_id, date, quantity, 1, seasonal_lag := 2);
```

---

## Advanced: Native Gap Filling

> **Note:** These functions provide high-performance gap filling with type preservation.
> For most use cases, use the simpler `ts_fill_gaps_by` table macro above.

### Supported Frequency Formats

All gap filling functions support multiple frequency formats:

| Format | Example | Description |
|--------|---------|-------------|
| Polars style | `'1d'`, `'1h'`, `'30m'` | Day, hour, minute |
| Weekly/Monthly | `'1w'`, `'1mo'`, `'1q'`, `'1y'` | Week, month, quarter, year |
| DuckDB INTERVAL | `'1 day'`, `'1 hour'` | Standard interval syntax |
| Raw integer | `'1'`, `'7'` | Interpreted as days |

---

## Advanced: Future Value Handling

> **Note:** These functions help handle unknown future values in cross-validation scenarios.

### ts_fill_unknown_by

Fill unknown future feature values based on a cutoff date.

**Signature:**
```sql
ts_fill_unknown_by(
    source VARCHAR,
    group_col COLUMN,
    date_col COLUMN,
    value_col COLUMN,
    cutoff_date DATE,
    params MAP
) → TABLE
```

**Params Options:**
| Key | Type | Default | Description |
|-----|------|---------|-------------|
| `strategy` | VARCHAR | `'last_value'` | `'null'`, `'last_value'`, or `'default'` |
| `fill_value` | DOUBLE | `0.0` | Value for `'default'` strategy |

**Strategies:**
| Strategy | Description |
|----------|-------------|
| `'null'` | Set future values to NULL |
| `'last_value'` | Use last known value before cutoff |
| `'default'` | Use specified `fill_value` |

**Example:**
```sql
-- Setup: weather_data(region, date, temperature), 2 regions x 130 daily rows.
CREATE OR REPLACE TABLE weather_data AS
SELECT
    region,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    15.0 + region * 3.0 + 8.0 * sin(2 * pi() * i / 365.0) AS temperature
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS region);
```

```sql
-- Fill unknown temperatures with last known value
SELECT * FROM ts_fill_unknown_by(
    'weather_data', region, date, temperature, '2020-03-01'::DATE,
    MAP{'strategy': 'last_value'}
);
```

---

### ts_mark_unknown_by

Mark rows as known/unknown based on a cutoff date.

**Signature:**
```sql
ts_mark_unknown_by(
    source VARCHAR,
    group_col COLUMN,
    date_col COLUMN,
    cutoff_date DATE
) → TABLE
```

**Output Columns:** All source columns plus:
| Column | Type | Description |
|--------|------|-------------|
| `is_unknown` | BOOLEAN | True for rows after cutoff |
| `last_known_date` | TIMESTAMP | Last date before cutoff (per group) |

**Example:**
```sql
-- Setup: future_value_data(product_id, date, competitor_price), 2 groups x 130 daily
-- rows (named distinctly from the global `sales_data` diagnostics fixture, which has
-- different columns, to avoid the checker's fixture-name shadowing).
CREATE OR REPLACE TABLE future_value_data AS
SELECT
    product_id,
    (DATE '2020-01-01' + INTERVAL (i) DAY) AS date,
    20.0 + product_id * 2.0 + 0.1 * i AS competitor_price
FROM range(0, 130) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS product_id);
```

```sql
-- Mark future rows and apply custom logic
SELECT
    *,
    CASE WHEN is_unknown THEN 0.0 ELSE competitor_price END AS price_masked
FROM ts_mark_unknown_by('future_value_data', product_id, date, '2020-03-01'::DATE);
```

---

## Advanced: Timestamp Validation

> **Note:** These functions validate that expected timestamps exist in your data.

### ts_validate_timestamps_by

Validate that expected timestamps exist in data for each group.

**Signature:**
```sql
ts_validate_timestamps_by(
    source VARCHAR,
    group_col COLUMN,
    date_col COLUMN,
    expected_timestamps DATE[]
) → TABLE
```

**Output Columns:**
| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Group identifier |
| `is_valid` | BOOLEAN | True if all timestamps found |
| `n_expected` | BIGINT | Number of expected timestamps |
| `n_found` | BIGINT | Number of timestamps found |
| `n_missing` | BIGINT | Number of missing timestamps |
| `missing_timestamps` | DATE[] | List of missing timestamps |

**Example:**
```sql
-- Validate that specific dates exist for each series
SELECT * FROM ts_validate_timestamps_by(
    'future_value_data', product_id, date,
    ['2024-01-01'::DATE, '2024-01-02'::DATE, '2024-01-03'::DATE]
) WHERE NOT is_valid;  -- Show only invalid series
```

---

### ts_validate_timestamps_summary_by

Quick validation summary across all groups.

**Signature:**
```sql
ts_validate_timestamps_summary_by(
    source VARCHAR,
    group_col COLUMN,
    date_col COLUMN,
    expected_timestamps DATE[]
) → TABLE
```

**Output Columns:**
| Column | Type | Description |
|--------|------|-------------|
| `all_valid` | BOOLEAN | True if all groups have all timestamps |
| `n_groups` | BIGINT | Total number of groups |
| `n_valid_groups` | BIGINT | Groups with all timestamps |
| `n_invalid_groups` | BIGINT | Groups missing timestamps |
| `invalid_groups` | ANY[] | List of invalid group IDs |

**Example:**
```sql
-- Quick check: are all series complete?
SELECT * FROM ts_validate_timestamps_summary_by(
    'future_value_data', product_id, date,
    ['2024-01-01'::DATE, '2024-01-02'::DATE, '2024-01-03'::DATE]
);
```

---

*See also: [Statistics](03-statistics.md) | [Forecasting](07-forecasting.md)*
