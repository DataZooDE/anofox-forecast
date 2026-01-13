# Decomposition Examples

> **Breaking down time series into trend, seasonal, and remainder components.**

This folder contains runnable SQL examples demonstrating time series decomposition with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_decomposition_examples.sql`](synthetic_decomposition_examples.sql) | 5 patterns using generated data | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/decomposition/synthetic_decomposition_examples.sql
```

---

## Overview

The extension provides functions for decomposing time series:

| Function | Algorithm | Best For |
|----------|-----------|----------|
| `ts_detrend` | Polynomial/linear | Remove trend |
| `ts_decompose_seasonal` | Classical decomposition | Single season |
| `ts_mstl_decomposition` | Multiple STL | Multi-seasonal |
| `_ts_mstl_decomposition` | Scalar MSTL | Array input |

---

## Patterns Overview

### Pattern 1: Detrending (ts_detrend)

**Use case:** Remove trend component to reveal seasonal patterns.

```sql
SELECT ts_detrend(LIST(value ORDER BY ts)) AS result
FROM my_series;
-- Returns: {trend, detrended, method, coefficients, rss, n_params}
```

**See:** `synthetic_decomposition_examples.sql` Section 1

---

### Pattern 2: Seasonal Decomposition (ts_decompose_seasonal)

**Use case:** Separate trend, seasonal, and remainder components.

```sql
-- Additive decomposition with period=12
SELECT ts_decompose_seasonal(LIST(value ORDER BY ts), 12, 'additive') AS result
FROM my_series;

-- Multiplicative decomposition
SELECT ts_decompose_seasonal(LIST(value ORDER BY ts), 12, 'multiplicative') AS result
FROM my_series;
```

**See:** `synthetic_decomposition_examples.sql` Section 2

---

### Pattern 3: MSTL Decomposition (Multi-Seasonal)

**Use case:** Handle multiple seasonal periods simultaneously.

```sql
-- Automatic multi-seasonal decomposition
SELECT _ts_mstl_decomposition(LIST(value ORDER BY ts)) AS result
FROM my_series;
-- Returns: {trend, seasonal[][], remainder, periods}
```

**See:** `synthetic_decomposition_examples.sql` Section 3

---

### Pattern 4: Decomposition for Grouped Series

**Use case:** Apply decomposition to multiple time series.

```sql
SELECT id, decomposition
FROM ts_mstl_decomposition('my_table', group_col, date_col, value_col, MAP{})
ORDER BY id;
```

**See:** `synthetic_decomposition_examples.sql` Section 4

---

### Pattern 5: Extract Components

**Use case:** Access individual decomposition components.

```sql
WITH decomposed AS (
    SELECT ts_decompose_seasonal(LIST(value ORDER BY ts), 12, 'additive') AS result
    FROM my_series
)
SELECT
    (result).trend AS trend_component,
    (result).seasonal AS seasonal_component,
    (result).remainder AS remainder_component
FROM decomposed;
```

**See:** `synthetic_decomposition_examples.sql` Section 5

---

## Output Structures

### ts_detrend

| Field | Type | Description |
|-------|------|-------------|
| `trend` | `DOUBLE[]` | Estimated trend component |
| `detrended` | `DOUBLE[]` | Original minus trend |
| `method` | `VARCHAR` | Detection method used |
| `coefficients` | `DOUBLE[]` | Trend coefficients |
| `rss` | `DOUBLE` | Residual sum of squares |
| `n_params` | `BIGINT` | Number of parameters |

### ts_decompose_seasonal

| Field | Type | Description |
|-------|------|-------------|
| `trend` | `DOUBLE[]` | Trend component |
| `seasonal` | `DOUBLE[]` | Seasonal component |
| `remainder` | `DOUBLE[]` | Residual component |
| `period` | `DOUBLE` | Period used |
| `method` | `VARCHAR` | Decomposition type |

### _ts_mstl_decomposition

| Field | Type | Description |
|-------|------|-------------|
| `trend` | `DOUBLE[]` | Trend component |
| `seasonal` | `DOUBLE[][]` | Seasonal components (one per period) |
| `remainder` | `DOUBLE[]` | Residual component |
| `periods` | `INTEGER[]` | Detected periods |

---

## Key Concepts

### Additive vs Multiplicative

| Type | Formula | Use When |
|------|---------|----------|
| **Additive** | Y = T + S + R | Constant seasonal amplitude |
| **Multiplicative** | Y = T × S × R | Amplitude grows with trend |

### Choosing the Right Method

| Data Characteristic | Recommended Function |
|---------------------|---------------------|
| Single seasonal period | `ts_decompose_seasonal` |
| Multiple seasonal periods | `ts_mstl_decomposition` |
| Just remove trend | `ts_detrend` |
| Grouped series | `ts_mstl_decomposition` table macro |

---

## Tips

1. **Know your period** - For classical decomposition, you need to specify the period.

2. **MSTL auto-detects** - The MSTL method can automatically detect multiple periods.

3. **Check remainder** - A good decomposition leaves only noise in the remainder.

4. **Use for forecasting** - Decompose, forecast components separately, then recombine.

5. **Minimum data** - Need at least 2× the period length for reliable decomposition.
