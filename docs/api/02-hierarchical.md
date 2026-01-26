# Hierarchical Time Series

> Multi-key hierarchy functions for aggregated forecasting

## Overview

When working with hierarchical time series data (e.g., region/store/item), you often need to combine multiple ID columns into a single `unique_id` for forecasting functions.

**Use this document to:**
- Combine multiple ID columns into a single `unique_id` for forecasting
- Create aggregated series at all hierarchy levels (total, region, store, item)
- Validate that separator characters don't conflict with your data
- Split combined keys back into original columns after forecasting
- Build hierarchical forecasting pipelines with proper aggregation

---

## Quick Start

```sql
-- Step 1: Validate separator is safe
SELECT * FROM ts_validate_separator('raw_sales', region_id, store_id, item_id);

-- Step 2: Create hierarchical time series with all aggregation levels
-- Supports arbitrary hierarchy depth (2-N levels)
CREATE TABLE prepared_data AS
SELECT * FROM ts_aggregate_hierarchy(
    (SELECT sale_date, quantity, region_id, store_id, item_id FROM raw_sales),
    MAP{}
);

-- Step 3: Forecast all series (original + aggregated)
CREATE TABLE forecasts AS
SELECT * FROM ts_forecast_by('prepared_data', unique_id, date, value,
    'AutoETS', 28, {'seasonal_period': 7});

-- Step 4: Split keys for analysis (with custom column names)
SELECT * FROM ts_split_keys(
    (SELECT unique_id, date, value FROM forecasts),
    columns := ['region_id', 'store_id', 'item_id']
);
```

---

## Workflow Summary

1. **Validate separator** - Check if your chosen separator is safe
2. **Combine keys** - Create a single unique_id from multiple columns (no aggregation)
3. **Aggregate hierarchy** - Combine keys AND create aggregated series at all levels
4. **Split keys** - Split unique_id back into original columns after forecasting

---

## Validate Separator

### ts_validate_separator

Checks if a separator character exists in any ID column values.

**Signature:**
```sql
ts_validate_separator(source, id_col1, [id_col2], [id_col3], [id_col4], [id_col5], separator := '|')
```

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `separator` | VARCHAR | The separator being validated |
| `is_valid` | BOOLEAN | True if separator is safe to use |
| `n_conflicts` | INTEGER | Number of values containing separator |
| `conflicting_values` | VARCHAR[] | Problematic values |
| `message` | VARCHAR | Helpful message if invalid |

**Example:**
```sql
-- Check default separator
SELECT * FROM ts_validate_separator('sales', region_id, store_id, item_id);

-- Check if dash is safe
SELECT * FROM ts_validate_separator('sales', region_id, store_id, separator := '-');
```

---

## Combine Keys

### ts_combine_keys

Native table function that combines multiple ID columns into a single `unique_id` column without creating aggregated series. Supports **arbitrary hierarchy levels** (2-N columns).

**Signature:**
```sql
ts_combine_keys(
    (SELECT date_col, value_col, id_col1, id_col2, ... FROM source),
    {'separator': '|'}  -- optional params
)
```

**Input Table:**
- First column: Date/timestamp column
- Second column: Value column
- Columns 3+: ID columns - **any number of columns**

**Parameters (optional MAP{}):**
- `separator`: Character(s) to join ID parts (default: `'|'`)

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `unique_id` | VARCHAR | Combined ID (e.g., `'EU|STORE001|SKU42'`) |
| `date_col` | (input type) | Date column (name preserved) |
| `value_col` | (input type) | Value column (name and type preserved) |

**Examples:**
```sql
-- Basic combination with 3 columns (with default params)
SELECT * FROM ts_combine_keys(
    (SELECT sale_date, quantity, region_id, store_id, item_id FROM sales),
    MAP{}
);
-- unique_id: 'EU|STORE001|SKU42'

-- Custom separator
SELECT * FROM ts_combine_keys(
    (SELECT sale_date, quantity, region_id, store_id FROM sales),
    MAP{'separator': '-'}
);
-- unique_id: 'EU-STORE001'

-- 5-level hierarchy
SELECT * FROM ts_combine_keys(
    (SELECT sale_date, quantity, country, region, city, store, item FROM sales),
    MAP{}
);
-- unique_id: 'US|West|Seattle|Store1|SKU42'
```

---

## Aggregate Hierarchy

### ts_aggregate_hierarchy

Native table function that supports **arbitrary hierarchy levels** (2-N columns).

**Signature:**
```sql
ts_aggregate_hierarchy(
    (SELECT date_col, value_col, id_col1, id_col2, ... FROM source),
    {'separator': '|', 'aggregate_keyword': 'AGGREGATED'}  -- optional params
)
```

**Input Table:**
- First column: Date/timestamp column
- Second column: Value column (will be summed at each aggregation level)
- Columns 3+: ID columns (broadest to finest granularity) - **any number of columns**

**Parameters (optional MAP{}):**
- `separator`: Character(s) to join ID parts (default: `'|'`)
- `aggregate_keyword`: Keyword for aggregated levels (default: `'AGGREGATED'`)

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `unique_id` | VARCHAR | Combined hierarchical ID |
| `date` | (input type) | Date column (preserved name and type) |
| `value` | DOUBLE | Aggregated value (preserved column name) |

**Aggregation Levels Created (for N ID columns):**

For N ID columns, generates N+1 aggregation levels:
| Level | Pattern (3 columns) | Description |
|-------|---------------------|-------------|
| 0 | `AGGREGATED|AGGREGATED|AGGREGATED` | Grand total |
| 1 | `EU|AGGREGATED|AGGREGATED` | Per first column |
| 2 | `EU|STORE001|AGGREGATED` | Per first two columns |
| 3 | `EU|STORE001|SKU42` | Original item level |

**Examples:**
```sql
-- 2-level hierarchy (region -> store)
SELECT * FROM ts_aggregate_hierarchy(
    (SELECT sale_date, quantity, region_id, store_id FROM sales),
    MAP{}
);

-- 3-level hierarchy (region -> store -> item)
SELECT * FROM ts_aggregate_hierarchy(
    (SELECT sale_date, quantity, region_id, store_id, item_id FROM sales),
    MAP{}
);

-- 4-level hierarchy (country -> region -> store -> item)
SELECT * FROM ts_aggregate_hierarchy(
    (SELECT sale_date, quantity, country_id, region_id, store_id, item_id FROM sales),
    MAP{}
);

-- Custom separator and keyword
SELECT * FROM ts_aggregate_hierarchy(
    (SELECT sale_date, quantity, region_id, store_id FROM sales),
    MAP{'separator': '::', 'aggregate_keyword': 'ALL'}
);
```

---

## Split Keys

### ts_split_keys

Native table function that splits a combined unique_id back into its original component columns. Auto-detects the number of parts from the data.

**Signature:**
```sql
ts_split_keys(
    (SELECT unique_id, date_col, value_col FROM source),
    separator := '|',                          -- optional
    columns := ['region', 'store', 'item']     -- optional
)
```

**Input Table:**
- First column: unique_id (VARCHAR)
- Second column: Date/timestamp column
- Third column: Value column

**Named Parameters (optional):**
- `separator`: Character(s) used to split (default: `'|'`)
- `columns`: LIST of column names for the split parts

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `id_part_1` or `<col1>` | VARCHAR | First component |
| `id_part_2` or `<col2>` | VARCHAR | Second component |
| `id_part_3` or `<col3>` | VARCHAR | Third component |
| ... | VARCHAR | Additional components |
| `date_col` | (input type) | Date column (preserved name) |
| `value_col` | (input type) | Value column (preserved name and type) |

**Examples:**
```sql
-- Default column names (id_part_1, id_part_2, id_part_3)
SELECT * FROM ts_split_keys(
    (SELECT unique_id, forecast_date, point_forecast FROM forecasts)
);

-- With custom column names
SELECT * FROM ts_split_keys(
    (SELECT unique_id, forecast_date, point_forecast FROM forecasts),
    columns := ['region_id', 'store_id', 'item_id']
);
-- Returns: region_id, store_id, item_id, forecast_date, point_forecast

-- Custom separator
SELECT * FROM ts_split_keys(
    (SELECT unique_id, ds, forecast FROM results),
    separator := '-'
);

-- Filter to store-level forecasts
SELECT * FROM ts_split_keys(
    (SELECT unique_id, ds, point_forecast FROM forecasts)
) WHERE id_part_3 = 'AGGREGATED' AND id_part_2 != 'AGGREGATED';
```

---

## Complete Workflow Example

```sql
-- Step 1: Validate separator
SELECT * FROM ts_validate_separator('raw_sales', region_id, store_id, item_id);

-- Step 2: Create aggregated time series (supports any number of hierarchy levels)
CREATE TABLE prepared_data AS
SELECT * FROM ts_aggregate_hierarchy(
    (SELECT sale_date, quantity, region_id, store_id, item_id FROM raw_sales),
    MAP{}
);

-- Step 3: Forecast all series (original + aggregated)
CREATE TABLE forecasts AS
SELECT * FROM ts_forecast_by('prepared_data', unique_id, sale_date, quantity,
    'AutoETS', 28, {'seasonal_period': 7});

-- Step 4: Split keys for analysis with original column names
SELECT *
FROM ts_split_keys(
    (SELECT unique_id, sale_date, quantity FROM forecasts),
    columns := ['region_id', 'store_id', 'item_id']
)
ORDER BY region_id, store_id, item_id, sale_date;
```

---

*See also: [Forecasting](07-forecasting.md) | [Cross-Validation](08-cross-validation.md)*
