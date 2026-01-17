# Data Preparation

> Filtering, cleaning, and imputation functions for time series

## Overview

Data preparation functions help clean and transform time series data before forecasting. These functions operate on arrays and return transformed arrays.

## Series Filtering

### ts_drop_constant

Filters out constant values from an array, returning NULL if all values are constant.

**Signature:**
```sql
ts_drop_constant(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_drop_constant([3.0, 3.0, 3.0, 3.0]);  -- Returns NULL
SELECT ts_drop_constant([1.0, 2.0, 3.0, 4.0]);  -- Returns [1.0, 2.0, 3.0, 4.0]
```

---

### ts_drop_short

Returns NULL if array length is below threshold.

**Signature:**
```sql
ts_drop_short(values DOUBLE[], min_length INTEGER) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_drop_short([1.0, 2.0, 3.0], 5);  -- Returns NULL (length < 5)
SELECT ts_drop_short([1.0, 2.0, 3.0, 4.0, 5.0], 5);  -- Returns the array
```

---

## Edge Cleaning

### ts_drop_leading_zeros

Removes leading zeros from an array.

**Signature:**
```sql
ts_drop_leading_zeros(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_drop_leading_zeros([0.0, 0.0, 1.0, 2.0, 3.0]);
-- Returns: [1.0, 2.0, 3.0]
```

---

### ts_drop_trailing_zeros

Removes trailing zeros from an array.

**Signature:**
```sql
ts_drop_trailing_zeros(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_drop_trailing_zeros([1.0, 2.0, 3.0, 0.0, 0.0]);
-- Returns: [1.0, 2.0, 3.0]
```

---

### ts_drop_edge_zeros

Removes both leading and trailing zeros from an array.

**Signature:**
```sql
ts_drop_edge_zeros(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_drop_edge_zeros([0.0, 0.0, 1.0, 2.0, 3.0, 0.0, 0.0]);
-- Returns: [1.0, 2.0, 3.0]
```

---

## Missing Value Imputation

### ts_fill_nulls_const

Replaces NULL values with a constant.

**Signature:**
```sql
ts_fill_nulls_const(values DOUBLE[], fill_value DOUBLE) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_fill_nulls_const([1.0, NULL, 3.0, NULL, 5.0], 0.0);
-- Returns: [1.0, 0.0, 3.0, 0.0, 5.0]
```

---

### ts_fill_nulls_forward

Forward fills NULL values (last observation carried forward).

**Signature:**
```sql
ts_fill_nulls_forward(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_fill_nulls_forward([1.0, NULL, NULL, 4.0, NULL]);
-- Returns: [1.0, 1.0, 1.0, 4.0, 4.0]
```

---

### ts_fill_nulls_backward

Backward fills NULL values.

**Signature:**
```sql
ts_fill_nulls_backward(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_fill_nulls_backward([NULL, NULL, 3.0, NULL, 5.0]);
-- Returns: [3.0, 3.0, 3.0, 5.0, 5.0]
```

---

### ts_fill_nulls_mean

Fills NULL values with the series mean.

**Signature:**
```sql
ts_fill_nulls_mean(values DOUBLE[]) → DOUBLE[]
```

**Example:**
```sql
SELECT ts_fill_nulls_mean([1.0, NULL, 3.0, NULL, 5.0]);
-- Returns: [1.0, 3.0, 3.0, 3.0, 5.0] (mean = 3.0)
```

---

## Differencing

### ts_diff

Computes differences of specified order.

**Signature:**
```sql
ts_diff(values DOUBLE[], order INTEGER) → DOUBLE[]
```

**Parameters:**
- `values`: Input array
- `order`: Difference order (must be > 0)

**Example:**
```sql
-- First differences
SELECT ts_diff([1.0, 2.0, 4.0, 7.0], 1);
-- Returns: [1.0, 2.0, 3.0]

-- Second differences
SELECT ts_diff([1.0, 2.0, 4.0, 7.0, 11.0], 2);
-- Returns: [1.0, 1.0, 1.0]
```

---

*See also: [Statistics](02-statistics.md) | [Forecasting](05-forecasting.md)*
