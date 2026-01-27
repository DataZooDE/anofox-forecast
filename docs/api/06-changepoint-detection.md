# Changepoint Detection

> Detect structural breaks in time series

## Overview

Changepoint detection identifies points in time where the statistical properties of a time series change significantly.

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
-- Multiple series
SELECT * FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    value,
    {'hazard_lambda': 100}
);
```

---

## Table Macros

### ts_detect_changepoints_by

Detect changepoints for each group in a table.

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
- `hazard_lambda`: Hazard rate parameter (default: 250.0)
- `include_probabilities`: Include per-point probabilities (default: false)

**Example:**
```sql
SELECT * FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    value,
    {'hazard_lambda': 100}
);
```

---

*See also: [Statistics](03-statistics.md) | [Feature Extraction](20-feature-extraction.md)*
