# Changepoint Detection

> Detect structural breaks in time series

## Overview

Changepoint detection identifies points in time where the statistical properties of a time series change significantly. The function returns row-level results, providing changepoint information for each data point in the input.

**Use this document to:**
- Detect structural breaks in mean, variance, or trend
- Find regime changes in sales, traffic, or sensor data
- Configure detection sensitivity with hazard rate parameter
- Get per-point changepoint probabilities for detailed analysis
- Identify series requiring model updates due to distribution shifts

---

## Quick Start

Detect changepoints using table macros:

```sql
-- Multiple series with row-level output
SELECT * FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    value,
    {'hazard_lambda': 100}
);

-- Filter to only changepoints
SELECT * FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    value,
    {'hazard_lambda': 100}
)
WHERE is_changepoint = true;
```

---

## Table Macros

### ts_detect_changepoints_by

Detect changepoints for each group in a table. Returns one row per data point with changepoint indicators.

**Signature:**
```sql
ts_detect_changepoints_by(source, group_col, date_col, value_col, params)
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `source` | VARCHAR | Source table name |
| `group_col` | COLUMN | Column for grouping series |
| `date_col` | COLUMN | Date/timestamp column |
| `value_col` | COLUMN | Value column |
| `params` | MAP | Configuration options |

**Parameters in MAP:**
- `hazard_lambda`: Hazard rate parameter (default: 250.0). Lower values detect more changepoints.

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier (column name preserved from input) |
| `<date_col>` | TIMESTAMP | Date/timestamp (column name preserved from input) |
| `is_changepoint` | BOOLEAN | Whether this point is a detected changepoint |
| `changepoint_probability` | DOUBLE | Probability of changepoint at this point (0-1) |

**Row Preservation:**

The function guarantees **output rows = input rows**. Rows that cannot be processed normally receive default values:

| Condition | is_changepoint | changepoint_probability |
|-----------|---------------|------------------------|
| NULL date | `false` | `NULL` |
| Group has < 2 data points | `false` | `NULL` |
| Processing error | `false` | `NULL` |

A warning is emitted when rows have NULL dates.

**Example:**
```sql
-- Detect changepoints with custom sensitivity
SELECT * FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    quantity,
    {'hazard_lambda': 100}
);

-- Find all changepoints with their dates
SELECT
    product_id,
    date,
    quantity,
    changepoint_probability
FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    quantity,
    {'hazard_lambda': 100}
)
WHERE is_changepoint = true
ORDER BY product_id, date;

-- Count changepoints per series
SELECT
    product_id,
    COUNT(*) FILTER (WHERE is_changepoint) AS n_changepoints
FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    quantity,
    {'hazard_lambda': 100}
)
GROUP BY product_id;
```

> **Alias:** `ts_detect_changepoints` is an alias for `ts_detect_changepoints_by`

---

*See also: [Statistics](03-statistics.md) | [Feature Extraction](20-feature-extraction.md)*
