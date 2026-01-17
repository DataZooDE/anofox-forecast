# Data Preparation

> Filtering, cleaning, and imputation table macros for time series

## Overview

Data preparation functions help clean and transform time series data before forecasting. These are **table macros** that operate on source tables and return transformed tables.

---

## Series Filtering

### ts_drop_constant

Filters out series where all values are constant.

**Signature:**
```sql
ts_drop_constant(source VARCHAR, group_col COLUMN, value_col COLUMN) → TABLE
```

**Example:**
```sql
-- Remove constant series from sales data
SELECT * FROM ts_drop_constant('sales', product_id, quantity);
```

---

### ts_drop_short

Filters out series shorter than the minimum length.

**Signature:**
```sql
ts_drop_short(source VARCHAR, group_col COLUMN, min_length INTEGER) → TABLE
```

**Example:**
```sql
-- Keep only series with at least 20 observations
SELECT * FROM ts_drop_short('sales', product_id, 20);
```

---

### ts_drop_gappy

Filters out series with too many gaps.

**Signature:**
```sql
ts_drop_gappy(source VARCHAR, group_col COLUMN, value_col COLUMN, max_gap_ratio DOUBLE) → TABLE
```

**Example:**
```sql
-- Remove series where gaps exceed 10% of data
SELECT * FROM ts_drop_gappy('sales', product_id, quantity, 0.1);
```

---

### ts_drop_zeros

Filters out series that are all zeros.

**Signature:**
```sql
ts_drop_zeros(source VARCHAR, group_col COLUMN, value_col COLUMN) → TABLE
```

---

## Edge Cleaning

### ts_drop_leading_zeros

Removes leading zeros from each series.

**Signature:**
```sql
ts_drop_leading_zeros(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

**Example:**
```sql
SELECT * FROM ts_drop_leading_zeros('sales', product_id, date, quantity);
```

---

### ts_drop_trailing_zeros

Removes trailing zeros from each series.

**Signature:**
```sql
ts_drop_trailing_zeros(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

---

### ts_drop_edge_zeros

Removes both leading and trailing zeros from each series.

**Signature:**
```sql
ts_drop_edge_zeros(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

---

## Missing Value Imputation

### ts_fill_nulls_const

Replaces NULL values with a constant.

**Signature:**
```sql
ts_fill_nulls_const(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, fill_value DOUBLE) → TABLE
```

**Example:**
```sql
-- Fill missing values with 0
SELECT * FROM ts_fill_nulls_const('sales', product_id, date, quantity, 0.0);
```

---

### ts_fill_nulls_forward

Forward fills NULL values (last observation carried forward).

**Signature:**
```sql
ts_fill_nulls_forward(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

**Example:**
```sql
SELECT * FROM ts_fill_nulls_forward('sales', product_id, date, quantity);
```

---

### ts_fill_nulls_backward

Backward fills NULL values.

**Signature:**
```sql
ts_fill_nulls_backward(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

---

### ts_fill_nulls_mean

Fills NULL values with the series mean.

**Signature:**
```sql
ts_fill_nulls_mean(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

---

## Gap Filling

### ts_fill_gaps

Fills gaps in time series by inserting rows for missing timestamps.

**Signature:**
```sql
ts_fill_gaps(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, frequency VARCHAR) → TABLE
```

**Parameters:**
- `frequency`: Time frequency string (e.g., `'1d'`, `'1h'`, `'1w'`)

**Example:**
```sql
-- Fill daily gaps
SELECT * FROM ts_fill_gaps('sales', product_id, date, quantity, '1d');
```

---

### ts_fill_forward

Forward fills to a target date.

**Signature:**
```sql
ts_fill_forward(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, target_date DATE, frequency VARCHAR) → TABLE
```

---

## Differencing

### ts_diff

Computes differences of specified order per series.

**Signature:**
```sql
ts_diff(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, diff_order INTEGER) → TABLE
```

**Parameters:**
- `diff_order`: Difference order (1 for first differences, 2 for second, etc.)

**Example:**
```sql
-- Compute first differences
SELECT * FROM ts_diff('sales', product_id, date, quantity, 1);

-- Compute second differences
SELECT * FROM ts_diff('sales', product_id, date, quantity, 2);
```

---

*See also: [Statistics](02-statistics.md) | [Forecasting](05-forecasting.md)*
