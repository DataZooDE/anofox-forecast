# Data Preparation

> Filtering, cleaning, and imputation table macros for time series

## Overview

Data preparation functions help clean and transform time series data before forecasting. These are **table macros** that operate on source tables and return transformed tables.

**Use this document to:**
- Fill gaps in irregular time series to ensure regular intervals
- Impute NULL values using constants, forward-fill, or interpolation
- Drop problematic series (constant, too short, too many NaNs/nulls)
- Differencing to remove trends before modeling
- Build data cleaning pipelines for production forecasting

---

## Quick Start

Common data cleaning pipeline (order matters for intermittent data):

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

Or chain operations:

```sql
-- Fill gaps and nulls first, then filter short series
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

Filters out series where all values are constant.

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

Forward fills NULL values (last observation carried forward).

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

Backward fills NULL values.

**Signature:**
```sql
ts_fill_nulls_backward_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

---

### ts_fill_nulls_mean_by

Fills NULL values with the series mean.

**Signature:**
```sql
ts_fill_nulls_mean_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

---

## Gap Filling

### ts_fill_gaps_by

Fills gaps in time series by inserting rows for missing timestamps.

**Signature:**
```sql
ts_fill_gaps_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, frequency VARCHAR) → TABLE
```

**Parameters:**
- `frequency`: Time frequency string (e.g., `'1d'`, `'1h'`, `'1w'`)

**Example:**
```sql
-- Fill daily gaps
SELECT * FROM ts_fill_gaps_by('sales', product_id, date, quantity, '1d');
```

---

### ts_fill_forward_by

Forward fills to a target date.

**Signature:**
```sql
ts_fill_forward_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, target_date DATE, frequency VARCHAR) → TABLE
```

---

## Differencing

### ts_diff_by

Computes differences of specified order per series.

**Signature:**
```sql
ts_diff_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, diff_order INTEGER) → TABLE
```

**Parameters:**
- `diff_order`: Difference order (1 for first differences, 2 for second, etc.)

**Example:**
```sql
-- Compute first differences
SELECT * FROM ts_diff_by('sales', product_id, date, quantity, 1);

-- Compute second differences
SELECT * FROM ts_diff_by('sales', product_id, date, quantity, 2);
```

---

## Advanced: Native Gap Filling

> **Note:** These functions provide high-performance gap filling with type preservation.
> For most use cases, use the simpler `ts_fill_gaps` table macro above.

### Supported Frequency Formats

All gap filling functions support multiple frequency formats:

| Format | Example | Description |
|--------|---------|-------------|
| Polars style | `'1d'`, `'1h'`, `'30m'` | Day, hour, minute |
| Weekly/Monthly | `'1w'`, `'1mo'`, `'1q'`, `'1y'` | Week, month, quarter, year |
| DuckDB INTERVAL | `'1 day'`, `'1 hour'` | Standard interval syntax |
| Raw integer | `'1'`, `'7'` | Interpreted as days |

---

### ts_fill_gaps_native

High-performance native gap filling with automatic type preservation.

> **Important:** This is a table-in-out function. Input must be passed as a subquery, not a table name.

**Signature:**
```sql
ts_fill_gaps_native(
    (SELECT group_col, date_col, value_col FROM source ORDER BY group_col, date_col),
    frequency VARCHAR
) → TABLE
```

**Type Preservation:** The date column type (DATE, TIMESTAMP, INTEGER, BIGINT) is automatically preserved.

**Example:**
```sql
-- CORRECT: Pass data as ordered subquery
SELECT * FROM ts_fill_gaps_native(
    (SELECT product_id, date, quantity FROM sales ORDER BY product_id, date),
    '1d'
);

-- WRONG: Cannot pass table name directly
SELECT * FROM ts_fill_gaps_native('sales', '1d');  -- ERROR!
```

---

### ts_fill_gaps_operator_by

Operator-compatible gap filling (same as `ts_fill_gaps` but named for operator compatibility).

**Signature:**
```sql
ts_fill_gaps_operator_by(
    source VARCHAR,
    group_col COLUMN,
    date_col COLUMN,
    value_col COLUMN,
    frequency VARCHAR
) → TABLE
```

---

### ts_fill_forward_native

Forward-fill time series to a target date with type preservation.

> **Important:** This is a table-in-out function. Input must be passed as a subquery.

**Signature:**
```sql
ts_fill_forward_native(
    (SELECT group_col, date_col, value_col FROM source ORDER BY group_col, date_col),
    target_date TIMESTAMP,
    frequency VARCHAR
) → TABLE
```

**Example:**
```sql
-- Extend series to end of year
SELECT * FROM ts_fill_forward_native(
    (SELECT store_id, date, sales FROM daily_sales ORDER BY store_id, date),
    '2024-12-31'::TIMESTAMP,
    '1d'
);
```

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
-- Fill unknown temperatures with last known value
SELECT * FROM ts_fill_unknown_by(
    'weather_data', region, date, temperature, '2024-06-01'::DATE,
    {'strategy': 'last_value'}
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
-- Mark future rows and apply custom logic
SELECT
    *,
    CASE WHEN is_unknown THEN 0.0 ELSE competitor_price END AS price_masked
FROM ts_mark_unknown_by('sales_data', product_id, date, '2024-06-15'::DATE);
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
    'sales_data', product_id, date,
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
    'sales_data', product_id, date,
    ['2024-01-01'::DATE, '2024-01-02'::DATE, '2024-01-03'::DATE]
);
```

---

*See also: [Statistics](03-statistics.md) | [Forecasting](07-forecasting.md)*
