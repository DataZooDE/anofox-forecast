# Gap Filling Examples

> **Complete your time series - fill gaps and extend to future dates.**

This folder contains runnable SQL examples demonstrating gap filling with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_gap_examples.sql`](synthetic_gap_examples.sql) | 5 patterns using generated data | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/gap_filling/synthetic_gap_examples.sql
```

---

## Overview

The extension provides functions for handling gaps in time series:

| Function | Purpose | Output Columns |
|----------|---------|----------------|
| `ts_fill_gaps_by` | Insert missing dates with NULL | group_col, date_col, value_col |
| `ts_fill_forward_by` | Extend series to target date | group_col, date_col, value_col |
| `ts_drop_gappy_by` | Remove series with too many gaps | Original columns |
| `ts_fill_unknown_by` | Fill future values for CV | group_col, date_col, value_col |

---

## Patterns Overview

### Pattern 1: Fill Date Gaps (ts_fill_gaps_by)

**Use case:** Insert missing dates in an irregular time series.

```sql
SELECT group_col, date_col, value_col
FROM ts_fill_gaps_by('my_table', series_id, date, value, '1 day')
ORDER BY group_col, date_col;
```

**See:** `synthetic_gap_examples.sql` Section 1

---

### Pattern 2: Fill Forward to Target Date (ts_fill_forward_by)

**Use case:** Extend series to a future date for forecasting.

```sql
SELECT group_col, date_col, value_col
FROM ts_fill_forward_by('my_table', series_id, date, value, '2024-12-31'::DATE, '1 day')
ORDER BY group_col, date_col;
```

**See:** `synthetic_gap_examples.sql` Section 2

---

### Pattern 3: Drop Gappy Series (ts_drop_gappy_by)

**Use case:** Remove series with too many missing values.

```sql
-- Keep only series with < 30% gaps
SELECT *
FROM ts_drop_gappy_by('my_table', series_id, value, 0.3);
```

**See:** `synthetic_gap_examples.sql` Section 3

---

### Pattern 4: Fill Unknown Future Values (ts_fill_unknown_by)

**Use case:** Handle future feature values in cross-validation.

```sql
-- Fill future values with last known value
SELECT group_col, date_col, value_col
FROM ts_fill_unknown_by('features', series_id, date, feature, '2024-01-15'::TIMESTAMP,
    MAP{'strategy': 'last_value'});
```

**See:** `synthetic_gap_examples.sql` Section 4

---

### Pattern 5: Different Frequency Formats

**Use case:** Support multiple frequency notation styles.

```sql
-- DuckDB interval format
SELECT * FROM ts_fill_gaps_by('data', id, ts, val, '1 day');
SELECT * FROM ts_fill_gaps_by('data', id, ts, val, '1 hour');
SELECT * FROM ts_fill_gaps_by('data', id, ts, val, '30 minute');

-- Polars-style format
SELECT * FROM ts_fill_gaps_by('data', id, ts, val, '1d');
SELECT * FROM ts_fill_gaps_by('data', id, ts, val, '1h');
SELECT * FROM ts_fill_gaps_by('data', id, ts, val, '30m');

-- Integer (days)
SELECT * FROM ts_fill_gaps_by('data', id, ts, val, 7);  -- 7 days
```

**See:** `synthetic_gap_examples.sql` Section 5

---

## Frequency Formats

| Format | Example | Description |
|--------|---------|-------------|
| DuckDB interval | `'1 day'`, `'1 hour'` | Standard SQL interval |
| Polars-style | `'1d'`, `'1h'`, `'30m'` | Compact notation |
| Integer | `1`, `7` | Days |

### Polars-style Suffixes

| Suffix | Meaning |
|--------|---------|
| `d` | Days |
| `h` | Hours |
| `m` or `min` | Minutes |
| `w` | Weeks |
| `mo` | Months |
| `q` | Quarters (3 months) |
| `y` | Years |

---

## ts_fill_unknown_by Strategies

| Strategy | Description |
|----------|-------------|
| `'last_value'` | Use last known value before cutoff |
| `'null'` | Replace with NULL |
| `'default'` | Use specified fill_value |

---

## Key Concepts

### Gap Ratio

The gap ratio is the proportion of NULL values in a series:

```
gap_ratio = n_nulls / n_total
```

Use `ts_drop_gappy_by` to filter out series with high gap ratios:

```sql
-- Keep series with < 50% gaps
SELECT * FROM ts_drop_gappy_by('data', id, value, 0.5);
```

### Cross-Validation with Unknown Future

In time series CV, features at future dates are unknown at prediction time. Use `ts_fill_unknown_by` to handle this:

```sql
-- Scenario: Train on data up to 2024-01-15, predict beyond
SELECT * FROM ts_fill_unknown_by('features', id, date, feature,
    '2024-01-15'::TIMESTAMP, MAP{'strategy': 'last_value'});
```

---

## Tips

1. **Fill gaps before imputation** - Insert missing dates first, then fill NULL values.

2. **Check gap ratio** - High gap ratio indicates poor data quality.

3. **Match frequency to data** - Use the correct interval for your data granularity.

4. **Cross-validation** - Use `ts_fill_unknown_by` for proper temporal CV.

5. **Output columns** - Most functions output `group_col`, `date_col`, `value_col`.
