# Decomposition

> Seasonal decomposition and classification

## Overview

Decomposition functions separate time series into trend, seasonal, and residual components. Classification functions analyze the type and strength of seasonality.

---

## Table Macros

### ts_mstl_decomposition_by

Multiple Seasonal-Trend decomposition using Loess (MSTL).

**Signature:**
```sql
ts_mstl_decomposition_by(source, group_col, date_col, value_col, seasonal_periods, params)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | IDENTIFIER | Series grouping column |
| `date_col` | IDENTIFIER | Date/timestamp column |
| `value_col` | IDENTIFIER | Value column |
| `seasonal_periods` | INTEGER[] | Array of seasonal periods |
| `params` | MAP | Additional parameters |

**Example:**
```sql
SELECT * FROM ts_mstl_decomposition_by(
    'sales', product_id, date, quantity,
    [7, 365],  -- weekly and yearly seasonality
    MAP{}
);
```

---

### ts_classify_seasonality_by

Classify seasonality type per group in a multi-series table.

**Signature:**
```sql
ts_classify_seasonality_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, period DOUBLE) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `period` | DOUBLE | Expected seasonal period |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `id` | (same as group_col) | Group identifier |
| `classification` | STRUCT | Classification results (see fields below) |

**Classification STRUCT fields:**
| Field | Type | Description |
|-------|------|-------------|
| `timing_classification` | VARCHAR | 'early', 'on_time', 'late', or 'variable' |
| `modulation_type` | VARCHAR | 'stable', 'growing', 'shrinking', or 'variable' |
| `has_stable_timing` | BOOLEAN | Whether peak timing is consistent |
| `timing_variability` | DOUBLE | Variability score (lower = more stable) |
| `seasonal_strength` | DOUBLE | Strength of seasonality (0-1) |
| `is_seasonal` | BOOLEAN | Whether significant seasonality exists |
| `cycle_strengths` | DOUBLE[] | Strength per cycle |
| `weak_seasons` | INTEGER[] | Indices of weak seasonal cycles |

**Example:**
```sql
-- Classify weekly seasonality per product
SELECT * FROM ts_classify_seasonality_by('sales', product_id, date, quantity, 7.0);

-- Find products with strong, stable seasonality
SELECT id, (classification).seasonal_strength
FROM ts_classify_seasonality_by('sales', product_id, date, quantity, 7.0)
WHERE (classification).is_seasonal AND (classification).has_stable_timing;
```

---

### ts_classify_seasonality

Classify the type and strength of seasonality for a single-series table.

**Signature:**
```sql
ts_classify_seasonality(source VARCHAR, date_col COLUMN, value_col COLUMN, period DOUBLE) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `period` | DOUBLE | Expected seasonal period |

**Returns:** Single row with `classification` STRUCT (same fields as above).

**Example:**
```sql
-- Classify weekly seasonality for a single series
SELECT * FROM ts_classify_seasonality('daily_sales', date, amount, 7.0);

-- Check if seasonality is strong and stable
SELECT
    (classification).is_seasonal,
    (classification).seasonal_strength,
    (classification).has_stable_timing
FROM ts_classify_seasonality('daily_sales', date, amount, 7.0);
```

---

## Aggregate Function

### ts_classify_seasonality_agg

Aggregate function for classifying seasonality with GROUP BY.

**Signature:**
```sql
ts_classify_seasonality_agg(date_col TIMESTAMP, value_col DOUBLE, period DOUBLE) → STRUCT
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `date_col` | TIMESTAMP | Date/timestamp column |
| `value_col` | DOUBLE | Value column |
| `period` | DOUBLE | Expected seasonal period |

**Returns:** Same STRUCT as `ts_classify_seasonality`.

**Example:**
```sql
-- Classify seasonality per product using GROUP BY
SELECT
    product_id,
    ts_classify_seasonality_agg(date, value, 7.0) AS classification
FROM sales
GROUP BY product_id;

-- Access specific fields
SELECT
    product_id,
    (ts_classify_seasonality_agg(date, value, 7.0)).is_seasonal,
    (ts_classify_seasonality_agg(date, value, 7.0)).seasonal_strength
FROM sales
GROUP BY product_id;
```

---

*See also: [Period Detection](04-period-detection.md) | [Peak Detection](04b-peak-detection.md) | [Internal Reference](04d-internal-period-functions.md)*
