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

**NULL handling (Phase 12, D-03/D-04):** the underlying `_ts_detect_changepoints_bocpd`
scalar shares the Rust single-series policy: leading/trailing `NULL` (and `NaN`) values
inside the `LIST(...)` are trimmed before the analysis runs; an interior `NULL` that
survives trimming is a hard error naming its 1-based position in the original list; a list
with no non-NULL value returns SQL `NULL`. The `is_changepoint`/`changepoint_probability`
arrays describe the **trimmed series** (position 1 = first non-NULL value) — the
single-series `ts_detect_changepoints` macro shifts its output back by the leading-NULL
row count so leading-NULL rows get `NULL` flags instead of misaligned ones.

```sql
-- Edge trim: leading/trailing NULLs give the same result as the trimmed series.
SELECT _ts_detect_changepoints_bocpd([NULL, 1.0, 2.0, 1.5, 8.0, 7.5, NULL]::DOUBLE[], 250.0, true)
     = _ts_detect_changepoints_bocpd([1.0, 2.0, 1.5, 8.0, 7.5]::DOUBLE[], 250.0, true);
-- true
```

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
    MAP{'hazard_lambda': '100'}
);

-- Filter to only changepoints
SELECT * FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    value,
    MAP{'hazard_lambda': '100'}
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
    MAP{'hazard_lambda': '100'}
);

-- Find all changepoints with their dates
-- (output columns are group_col/date_col/is_changepoint/changepoint_probability only --
-- the value column is NOT included in ts_detect_changepoints_by's output)
SELECT
    product_id,
    date,
    changepoint_probability
FROM ts_detect_changepoints_by(
    'sales',
    product_id,
    date,
    quantity,
    MAP{'hazard_lambda': '100'}
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
    MAP{'hazard_lambda': '100'}
)
GROUP BY product_id;
```

> **Not an alias:** `ts_detect_changepoints` is a **different, single-series** macro, not an
> alias for `ts_detect_changepoints_by`. It takes 4 args — `(source, date_col, value_col,
> params)`, no `group_col` — and its output shape differs: `(date_col, value_col,
> is_changepoint, changepoint_probability)`. Notably it INCLUDES the value column (unlike
> `ts_detect_changepoints_by`, which omits it) and has no series identifier column at all.

---

*See also: [Statistics](03-statistics.md) | [Feature Extraction](20-feature-extraction.md)*
