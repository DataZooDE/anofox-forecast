# Data Preparation Examples

> **Clean data is the foundation of accurate forecasts - prepare it well.**

This folder contains runnable SQL examples demonstrating time series data preparation with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_data_prep_examples.sql`](synthetic_data_prep_examples.sql) | 10 patterns using generated data | Synthetic |

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
| **Filtering** | `ts_drop_constant`, `ts_drop_short`, `ts_drop_zeros`, `ts_drop_gappy` | Remove unusable series |
| **Edge Cleaning** | `ts_drop_leading_zeros`, `ts_drop_trailing_zeros`, `ts_drop_edge_zeros` | Clean edge artifacts |
| **Imputation** | `ts_fill_nulls_const`, `ts_fill_nulls_forward`, `ts_fill_nulls_backward`, `ts_fill_nulls_mean` | Handle missing values |
| **Transform** | `ts_diff` | Differencing for stationarity |
| **Statistics** | `ts_stats`, `ts_stats_summary` | Dataset health overview |
| **Quality** | `ts_data_quality`, `ts_data_quality_summary` | Quality tier assessment |
| **Validation** | `ts_validate_timestamps`, `ts_validate_timestamps_summary` | Timestamp completeness |

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

### Pattern 7: Statistics Summary

**Use case:** Get a quick overview of dataset characteristics.

```sql
-- Step 1: Compute per-series stats
CREATE TABLE computed_stats AS
SELECT * FROM ts_stats('sales_data', series_id, ts, value, '1d');

-- Step 2: Aggregate to dataset-wide summary
SELECT
    COUNT(*) AS n_series,
    AVG((stats).length) AS avg_length,
    MIN((stats).length) AS min_length,
    MAX((stats).length) AS max_length,
    SUM((stats).n_nulls) AS total_nulls
FROM computed_stats;
```

**When to use:**
- `ts_stats` - Investigate individual series issues
- `ts_stats_summary` - Quick dataset health check

**See:** `synthetic_data_prep_examples.sql` Section 7

---

### Pattern 8: Data Quality Summary

**Use case:** Assess data quality distribution across all series.

```sql
-- Per-series quality scores
SELECT unique_id, (quality).overall_score
FROM ts_data_quality('sales_data', series_id, ts, value, 10, '1d');

-- Dataset-wide quality tier counts
SELECT * FROM ts_data_quality_summary('sales_data', series_id, ts, value, 10);
-- Returns: n_total, n_good (>=0.8), n_fair (0.5-0.8), n_poor (<0.5), avg_score
```

**When to use:**
- `ts_data_quality` - Identify which series need attention
- `ts_data_quality_summary` - Quick pass/fail assessment for bulk processing

**See:** `synthetic_data_prep_examples.sql` Section 8

---

### Pattern 9: Drop Zeros vs Drop Gappy

**Use case:** Filter inactive vs sparse series.

```sql
-- Remove series with ALL zero values (inactive products)
SELECT * FROM ts_drop_zeros('sales_data', series_id, value);

-- Remove series with >30% NULL values (sparse data)
SELECT * FROM ts_drop_gappy('sales_data', series_id, value, 0.3);
```

**When to use:**
- `ts_drop_zeros` - Remove truly inactive series (all zeros)
- `ts_drop_gappy` - Configurable threshold for null ratio

**See:** `synthetic_data_prep_examples.sql` Section 9

---

### Pattern 10: Timestamp Validation

**Use case:** Verify all expected timestamps exist before forecasting.

```sql
-- Detailed per-group validation (which timestamps missing)
SELECT group_col, is_valid, missing_timestamps
FROM ts_validate_timestamps(
    'sales_data', series_id, ts,
    ['2024-01-01'::TIMESTAMP, '2024-01-02'::TIMESTAMP, '2024-01-03'::TIMESTAMP]
);

-- Quick dataset-wide check (pass/fail)
SELECT * FROM ts_validate_timestamps_summary(
    'sales_data', series_id, ts,
    ['2024-01-01'::TIMESTAMP, '2024-01-02'::TIMESTAMP, '2024-01-03'::TIMESTAMP]
);
-- Returns: all_valid, n_groups, n_valid_groups, n_invalid_groups, invalid_groups
```

**When to use:**
- `ts_validate_timestamps` - Debug missing data per series
- `ts_validate_timestamps_summary` - Quick validation before bulk processing

**See:** `synthetic_data_prep_examples.sql` Section 10

---

### When to Use Summary vs Detail Functions

| Need | Function | Returns |
|------|----------|---------|
| Per-series stats | `ts_stats` | 1 row per series |
| Dataset overview | `ts_stats_summary` | 1 aggregate row |
| Per-series quality | `ts_data_quality` | 1 row per series |
| Quality distribution | `ts_data_quality_summary` | Tier counts (good/fair/poor) |
| Remove inactive series | `ts_drop_zeros` | Filter: all-zero |
| Remove sparse series | `ts_drop_gappy` | Filter: configurable null ratio |
| Which timestamps missing | `ts_validate_timestamps` | Per-group with missing list |
| Quick validation check | `ts_validate_timestamps_summary` | Pass/fail + invalid group names |

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
