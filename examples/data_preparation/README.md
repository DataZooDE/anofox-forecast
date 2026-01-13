# Data Preparation Examples

> **Clean data is the foundation of accurate forecasts - prepare it well.**

This folder contains runnable SQL examples demonstrating time series data preparation with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_data_prep_examples.sql`](synthetic_data_prep_examples.sql) | 6 patterns using generated data | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/data_preparation/synthetic_data_prep_examples.sql
```

---

## Overview

The extension provides functions for cleaning and preparing time series data:

| Category | Functions | Purpose |
|----------|-----------|---------|
| **Filtering** | `ts_drop_constant`, `ts_drop_short` | Remove unusable series |
| **Edge Cleaning** | `ts_drop_leading_zeros`, `ts_drop_trailing_zeros`, `ts_drop_edge_zeros` | Clean edge artifacts |
| **Imputation** | `ts_fill_nulls_const`, `ts_fill_nulls_forward`, `ts_fill_nulls_backward`, `ts_fill_nulls_mean` | Handle missing values |
| **Transform** | `ts_diff` | Differencing for stationarity |

---

## Patterns Overview

### Pattern 1: Drop Constant Series

**Use case:** Remove series with no variation (can't be forecast).

```sql
SELECT ts_drop_constant([3.0, 3.0, 3.0, 3.0]);  -- Returns NULL
SELECT ts_drop_constant([1.0, 2.0, 3.0, 4.0]);  -- Returns the array
```

**See:** `synthetic_data_prep_examples.sql` Section 1

---

### Pattern 2: Drop Short Series

**Use case:** Remove series too short for reliable forecasting.

```sql
SELECT ts_drop_short([1.0, 2.0, 3.0], 5);  -- Returns NULL (length < 5)
SELECT ts_drop_short([1.0, 2.0, 3.0, 4.0, 5.0], 5);  -- Returns array
```

**See:** `synthetic_data_prep_examples.sql` Section 2

---

### Pattern 3: Clean Edge Zeros

**Use case:** Remove leading/trailing zeros from demand data.

```sql
SELECT ts_drop_leading_zeros([0.0, 0.0, 1.0, 2.0, 3.0]);  -- [1.0, 2.0, 3.0]
SELECT ts_drop_trailing_zeros([1.0, 2.0, 3.0, 0.0, 0.0]);  -- [1.0, 2.0, 3.0]
SELECT ts_drop_edge_zeros([0.0, 0.0, 1.0, 2.0, 0.0, 0.0]);  -- [1.0, 2.0]
```

**See:** `synthetic_data_prep_examples.sql` Section 3

---

### Pattern 4: Fill Missing Values (Imputation)

**Use case:** Handle NULLs with various strategies.

```sql
-- Fill with constant
SELECT ts_fill_nulls_const([1.0, NULL, 3.0], 0.0);  -- [1.0, 0.0, 3.0]

-- Forward fill (LOCF)
SELECT ts_fill_nulls_forward([1.0, NULL, NULL, 4.0]);  -- [1.0, 1.0, 1.0, 4.0]

-- Backward fill
SELECT ts_fill_nulls_backward([NULL, NULL, 3.0]);  -- [3.0, 3.0, 3.0]

-- Fill with mean
SELECT ts_fill_nulls_mean([1.0, NULL, 5.0]);  -- [1.0, 3.0, 5.0]
```

**See:** `synthetic_data_prep_examples.sql` Section 4

---

### Pattern 5: Differencing

**Use case:** Make series stationary for ARIMA models.

```sql
-- First difference
SELECT ts_diff([1.0, 2.0, 4.0, 7.0], 1);  -- [1.0, 2.0, 3.0]

-- Second difference
SELECT ts_diff([1.0, 2.0, 4.0, 7.0, 11.0], 2);  -- [1.0, 1.0, 1.0]
```

**See:** `synthetic_data_prep_examples.sql` Section 5

---

### Pattern 6: Complete Pipeline

**Use case:** Chain multiple preparation steps.

```sql
WITH cleaned AS (
    SELECT
        product_id,
        ts_drop_short(
            ts_fill_nulls_forward(LIST(value ORDER BY date)),
            10
        ) AS values
    FROM sales
    GROUP BY product_id
)
SELECT * FROM cleaned WHERE values IS NOT NULL;
```

**See:** `synthetic_data_prep_examples.sql` Section 6

---

## Key Concepts

### Imputation Strategies

| Strategy | Function | Best For |
|----------|----------|----------|
| **Constant** | `ts_fill_nulls_const` | Event indicators (fill with 0) |
| **Forward Fill** | `ts_fill_nulls_forward` | Slowly changing values |
| **Backward Fill** | `ts_fill_nulls_backward` | Known future values |
| **Mean** | `ts_fill_nulls_mean` | Stationary data |

### Filtering Functions

| Function | Returns | Use Case |
|----------|---------|----------|
| `ts_drop_constant` | NULL if constant | Remove unforecastable |
| `ts_drop_short` | NULL if too short | Require min history |

### Edge Cleaning

| Function | Action |
|----------|--------|
| `ts_drop_leading_zeros` | Remove zeros at start |
| `ts_drop_trailing_zeros` | Remove zeros at end |
| `ts_drop_edge_zeros` | Remove zeros at both ends |

---

## Tips

1. **Check for constants first** - Constant series waste computation.

2. **Forward fill is safe** - Doesn't use future data (no look-ahead).

3. **Mind the order** - Clean edges before imputation.

4. **Verify after cleaning** - Check series lengths after preparation.

5. **Differencing reduces length** - First diff loses 1 point.

---

## Troubleshooting

### Q: Why did my series become NULL?

**A:** It was either constant or too short. Check:
```sql
SELECT product_id, COUNT(*), STDDEV(value)
FROM sales GROUP BY product_id HAVING STDDEV(value) = 0;
```

### Q: Which imputation method should I use?

**A:**
- **Time series**: Forward fill (LOCF)
- **Cross-sectional**: Mean
- **Binary events**: Constant (0)
- **Centered data**: Mean

### Q: How much differencing do I need?

**A:** Usually 1 for trend, 2 for acceleration. Check with:
```sql
-- If first diff still has trend, try second diff
SELECT ts_diff(values, 1), ts_diff(values, 2) FROM data;
```
