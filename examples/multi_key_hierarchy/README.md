# Multi-Key Hierarchy Examples

> **Handle composite keys and hierarchical aggregation for time series forecasting.**

This folder contains runnable SQL examples demonstrating multi-key unique_id handling with the anofox-forecast extension.

## Example Files

| File | Description | Data Source |
|------|-------------|-------------|
| [`synthetic_multi_key_examples.sql`](synthetic_multi_key_examples.sql) | 5 patterns using generated data | Synthetic |

## Quick Start

```bash
# Run synthetic examples
./build/release/duckdb < examples/multi_key_hierarchy/synthetic_multi_key_examples.sql
```

---

## Overview

When working with hierarchical time series data (e.g., region/store/item), you often need to:

1. **Combine multiple ID columns** into a single `unique_id` for forecasting functions
2. **Create aggregated series** at different hierarchy levels (store totals, region totals, grand totals)
3. **Split the combined ID** back into original columns after forecasting

The extension provides these functions:

| Function | Purpose | Output |
|----------|---------|--------|
| `ts_validate_separator` | Check if separator is safe | Validation result |
| `ts_combine_keys` | Combine ID columns into unique_id | unique_id, date_col, value_col |
| `ts_aggregate_hierarchy` | Combine + aggregate at all levels | unique_id, date_col, value_col |
| `ts_split_keys` | Split unique_id back into parts | id_part_1, id_part_2, id_part_3, ... |

---

## Patterns Overview

### Pattern 1: Validate Separator

**Use case:** Check if your chosen separator exists in any ID values before combining.

```sql
SELECT * FROM ts_validate_separator(
    'sales_data',
    region_id, store_id, item_id,
    separator := '|'
);
-- Returns: separator, is_valid, n_conflicts, conflicting_values, message
```

**See:** `synthetic_multi_key_examples.sql` Section 1

---

### Pattern 2: Combine Keys (No Aggregation)

**Use case:** Simple key combination without creating aggregated series.

```sql
SELECT * FROM ts_combine_keys(
    'sales_data',
    sale_date,
    quantity,
    region_id, store_id, item_id
);
-- Output: unique_id (e.g., 'EU|STORE001|SKU42'), sale_date, quantity
```

**See:** `synthetic_multi_key_examples.sql` Section 2

---

### Pattern 3: Hierarchical Aggregation (Main Function)

**Use case:** Combine keys AND create aggregated series at all hierarchy levels.

```sql
SELECT * FROM ts_aggregate_hierarchy(
    'sales_data',
    sale_date,
    quantity,
    region_id, store_id, item_id
);
-- Output includes:
-- EU|STORE001|SKU42        (original item)
-- EU|STORE001|AGGREGATED   (store total)
-- EU|AGGREGATED|AGGREGATED (region total)
-- AGGREGATED|AGGREGATED|AGGREGATED (grand total)
```

**See:** `synthetic_multi_key_examples.sql` Section 3

---

### Pattern 4: Split Keys (Reverse Operation)

**Use case:** Split combined unique_id back into columns after forecasting.

```sql
SELECT * FROM ts_split_keys(
    'forecast_results',
    unique_id,
    forecast_date,
    point_forecast
);
-- Output: id_part_1, id_part_2, id_part_3, date_col, value_col
```

**See:** `synthetic_multi_key_examples.sql` Section 4

---

### Pattern 5: End-to-End Workflow

**Use case:** Complete workflow from raw data to split forecast results.

```sql
-- Step 1: Create aggregated time series
CREATE TABLE prepared_data AS
SELECT * FROM ts_aggregate_hierarchy(
    'raw_sales', sale_date, quantity, region_id, store_id, item_id
);

-- Step 2: Forecast all series (original + aggregated)
CREATE TABLE forecasts AS
SELECT * FROM ts_forecast_by(
    'prepared_data', unique_id, date_col, value_col,
    'AutoETS', 28, MAP{'seasonal_period': '7'}
);

-- Step 3: Split keys for analysis
SELECT * FROM ts_split_keys(
    'forecasts', id, ds, point_forecast
);
```

**See:** `synthetic_multi_key_examples.sql` Section 5

---

## Parameters

### ts_validate_separator

| Parameter | Default | Description |
|-----------|---------|-------------|
| `source` | Required | Table name |
| `id_col1` | Required | First ID column |
| `id_col2-5` | NULL | Optional additional ID columns |
| `separator` | `\|` | Separator to validate |

### ts_combine_keys

| Parameter | Default | Description |
|-----------|---------|-------------|
| `source` | Required | Table name |
| `date_col` | Required | Date/timestamp column |
| `value_col` | Required | Value column |
| `id_col1` | Required | First ID column |
| `id_col2-5` | NULL | Optional additional ID columns |
| `params` | `MAP{}` | Options: `separator` (default '\|') |

### ts_aggregate_hierarchy

| Parameter | Default | Description |
|-----------|---------|-------------|
| `source` | Required | Table name |
| `date_col` | Required | Date/timestamp column |
| `value_col` | Required | Value column |
| `id_col1` | Required | First ID column |
| `id_col2` | Required | Second ID column |
| `id_col3` | Required | Third ID column |
| `params` | `MAP{}` | Options: `separator` (default '\|'), `aggregate_keyword` (default 'AGGREGATED') |

### ts_split_keys

| Parameter | Default | Description |
|-----------|---------|-------------|
| `source` | Required | Table name |
| `id_col` | Required | Combined unique_id column |
| `date_col` | Required | Date column |
| `value_col` | Required | Value column |
| `separator` | `\|` | Separator used in unique_id |

---

## Aggregation Levels

For a hierarchy with 3 columns `[region, store, item]`:

| Level | unique_id Pattern | Description |
|-------|-------------------|-------------|
| 0 | `AGGREGATED\|AGGREGATED\|AGGREGATED` | Grand total |
| 1 | `EU\|AGGREGATED\|AGGREGATED` | Per region |
| 2 | `EU\|STORE001\|AGGREGATED` | Per store |
| 3 | `EU\|STORE001\|SKU42` | Original item |

All levels are included by default when using `ts_aggregate_hierarchy`.

---

## Separator Options

| Separator | Use When |
|-----------|----------|
| `\|` (default) | IDs don't contain pipe characters |
| `-` | IDs may contain pipes but not dashes |
| `::` | Need multi-character separator |
| `__` | Prefer underscores |

Always validate with `ts_validate_separator` first!

---

## Tips

1. **Validate first**: Always use `ts_validate_separator` before combining keys to avoid malformed unique_ids.

2. **Aggregation order matters**: Columns are aggregated hierarchically left-to-right. Put broader categories first (region before store before item).

3. **Filter by level**: After aggregation, filter results by `unique_id LIKE '%AGGREGATED%'` patterns to get specific levels.

4. **Rename after split**: Use column aliases after `ts_split_keys` to restore original column names:
   ```sql
   SELECT
       id_part_1 AS region_id,
       id_part_2 AS store_id,
       id_part_3 AS item_id,
       date_col, value_col
   FROM ts_split_keys(...);
   ```

5. **Memory consideration**: Hierarchical aggregation creates multiple series per original item. With N hierarchy levels, you get ~N additional series per date.
