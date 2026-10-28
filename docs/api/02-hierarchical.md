# Hierarchical Time Series

> Multi-key hierarchy functions for aggregated forecasting

## Overview

When working with hierarchical time series data (e.g., region/store/item), you often need to combine multiple ID columns into a single `unique_id` for forecasting functions.

`ts_aggregate_hierarchy`, `ts_split_keys` and `ts_validate_separator` read their whole input regardless of plan shape (a UNION ALL source included); call signatures are unchanged (#285).

**Use this document to:**
- Combine multiple ID columns into a single `unique_id` for forecasting
- Create aggregated series at all hierarchy levels (total, region, store, item)
- Validate that separator characters don't conflict with your data
- Split combined keys back into original columns after forecasting
- Build hierarchical forecasting pipelines with proper aggregation

---

## Quick Start

```sql
-- Setup: raw_sales(sale_date, quantity, region_id, store_id, item_id, country_id,
-- country, region, state, city, store, item), 2 regions x 60 daily rows -- the extra
-- country/region/state/city/store/item columns feed the 5-/6-level hierarchy examples
-- further down this file.
CREATE TABLE raw_sales AS
SELECT
    (DATE '2024-01-01' + INTERVAL (i) DAY) AS sale_date,
    10.0 + (i % 7) + region_num * 2.0 AS quantity,
    'R' || region_num AS region_id,
    'STORE00' || region_num AS store_id,
    'SKU' || region_num AS item_id,
    'US' AS country_id,
    'US' AS country,
    'R' || region_num AS region,
    'ST' || region_num AS state,
    'City' || region_num AS city,
    'Store' || region_num AS store,
    'SKU' || region_num AS item
FROM range(0, 60) t(i)
CROSS JOIN (SELECT unnest([1, 2]) AS region_num);
```

```sql
-- Step 1: Validate separator is safe
SELECT * FROM ts_validate_separator(
    (SELECT region_id, store_id, item_id FROM raw_sales)
);
```

```sql
-- Step 2: Create hierarchical time series with all aggregation levels
-- Supports arbitrary hierarchy depth (2-N levels)
CREATE TABLE prepared_data AS
SELECT * FROM ts_aggregate_hierarchy(
    (SELECT sale_date, quantity, region_id, store_id, item_id FROM raw_sales),
    MAP{}
);
```

```sql
-- Step 3: Forecast all series (original + aggregated) -- ts_aggregate_hierarchy
-- preserves the input date/value column names (sale_date, quantity), it does not
-- rename them to "date"/"value"
CREATE TABLE forecasts AS
SELECT * FROM ts_forecast_by('prepared_data', unique_id, sale_date, quantity,
    'AutoETS', 28, '1d', MAP{'seasonal_period': '7'});
```

```sql
-- Step 4: Split keys for analysis (with custom column names)
SELECT * FROM ts_split_keys(
    (SELECT unique_id, ds, yhat FROM forecasts),
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

Native table function that checks if a separator character exists in any ID column values. Supports **arbitrary number of ID columns**.

**Signature:**
```sql
ts_validate_separator(
    (SELECT id_col1, id_col2, ... FROM source),
    separator := '|'  -- optional
)
```

**Input Table:**
- All columns are treated as ID columns to validate

**Named Parameters (optional):**
- `separator`: Character(s) to validate (default: `'|'`)

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `separator` | VARCHAR | The separator being validated |
| `is_valid` | BOOLEAN | True if separator is safe to use |
| `n_conflicts` | INTEGER | Number of values containing separator |
| `conflicting_values` | VARCHAR[] | Problematic values |
| `message` | VARCHAR | Helpful message if invalid |

**Examples:**
```sql
-- Check default separator with 3 columns
SELECT * FROM ts_validate_separator(
    (SELECT region_id, store_id, item_id FROM raw_sales)
);

-- Check if dash is safe
SELECT * FROM ts_validate_separator(
    (SELECT region_id, store_id FROM raw_sales),
    separator := '-'
);

-- Check with 6-level hierarchy
SELECT * FROM ts_validate_separator(
    (SELECT country, region, state, city, store, item FROM raw_sales)
);
```

---

## Combine Keys

### ts_combine_keys

Native table function that combines multiple ID columns into a single `unique_id` column without creating aggregated series. Supports **arbitrary hierarchy levels** (2-N columns).

**Signature:**
```sql
ts_combine_keys(
    (SELECT date_col, value_col, id_col1, id_col2, ... FROM source),
    MAP{'separator': '|'}  -- optional params
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
    (SELECT sale_date, quantity, region_id, store_id, item_id FROM raw_sales),
    MAP{}
);
-- unique_id: 'EU|STORE001|SKU42'

-- Custom separator
SELECT * FROM ts_combine_keys(
    (SELECT sale_date, quantity, region_id, store_id FROM raw_sales),
    MAP{'separator': '-'}
);
-- unique_id: 'EU-STORE001'

-- 5-level hierarchy
SELECT * FROM ts_combine_keys(
    (SELECT sale_date, quantity, country, region, city, store, item FROM raw_sales),
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
    MAP{'separator': '|', 'aggregate_keyword': 'AGGREGATED'}  -- optional params
)
```

**Input Table:**
- First column: Date/timestamp column
- Second column: Value column, summed at each aggregation level using exact SQL `SUM` NULL
  semantics: a group whose leaves are **all** `NULL` for a given date aggregates to `NULL`
  (no `NULL -> 0` imputation); a **partially-NULL** group aggregates to the sum of only its
  present (non-`NULL`) leaf values.
- Columns 3+: ID columns (broadest to finest granularity) - **any number of columns**

Example — `S1` has leaves `A=10.0` and `B=NULL`; `S2` has only leaf `C=NULL`:
```sql
SELECT unique_id, quantity FROM ts_aggregate_hierarchy(
    (SELECT sale_date, quantity, store_id, item_id FROM (VALUES
        ('S1', 'A', DATE '2024-01-01', 10.0),
        ('S1', 'B', DATE '2024-01-01', NULL),
        ('S2', 'C', DATE '2024-01-01', NULL)
     ) AS t(store_id, item_id, sale_date, quantity)),
    MAP{}
) WHERE unique_id IN ('S1|AGGREGATED', 'S2|AGGREGATED');
-- S1|AGGREGATED -> 10.0 (sum of the one present leaf)
-- S2|AGGREGATED -> NULL (no leaf was present)
```

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
    (SELECT sale_date, quantity, region_id, store_id FROM raw_sales),
    MAP{}
);

-- 3-level hierarchy (region -> store -> item)
SELECT * FROM ts_aggregate_hierarchy(
    (SELECT sale_date, quantity, region_id, store_id, item_id FROM raw_sales),
    MAP{}
);

-- 4-level hierarchy (country -> region -> store -> item)
SELECT * FROM ts_aggregate_hierarchy(
    (SELECT sale_date, quantity, country_id, region_id, store_id, item_id FROM raw_sales),
    MAP{}
);

-- Custom separator and keyword
SELECT * FROM ts_aggregate_hierarchy(
    (SELECT sale_date, quantity, region_id, store_id FROM raw_sales),
    MAP{'separator': '::', 'aggregate_keyword': 'ALL'}
);
```

### All levels with GROUP BY ROLLUP

The same series can be built with plain SQL: `GROUP BY date, ROLLUP(...)` sums every
level in one aggregation, and `GROUPING()` marks the rolled-up keys. The result has the
same `unique_id`, date and value rows as `ts_aggregate_hierarchy` (tested in
`test/sql/ts_grouping_sets.test`). This form is handy when the level values need another
aggregate than `SUM`, or extra columns per level. On DuckDB 2.0 the coarser levels are
computed from the finer ones (cascading aggregation).

```sql
CREATE TABLE prepared_rollup AS
SELECT CASE WHEN GROUPING(region_id) = 1 THEN 'AGGREGATED' ELSE region_id END || '|' ||
       CASE WHEN GROUPING(store_id) = 1 THEN 'AGGREGATED' ELSE store_id END || '|' ||
       CASE WHEN GROUPING(item_id) = 1 THEN 'AGGREGATED' ELSE item_id END AS unique_id,
       sale_date,
       SUM(quantity) AS quantity
FROM raw_sales
GROUP BY sale_date, ROLLUP(region_id, store_id, item_id);

-- forecast every level in one call
SELECT * FROM ts_forecast_by('prepared_rollup', unique_id, sale_date, quantity, 'Naive', 7, '1d');
```

The `ts_*_agg` aggregates also work under `ROLLUP` / `GROUPING SETS` / `CUBE` and give
the same per-level results as separate `GROUP BY` queries, e.g. series statistics for
every store, every region and overall in one pass:

```sql
SELECT region_id, store_id, ts_stats_agg(sale_date::TIMESTAMP, quantity) AS stats
FROM raw_sales
GROUP BY ROLLUP(region_id, store_id);
```

Here a region's series holds the raw rows of all its stores. Sum per date first (as in
`prepared_rollup`) when the level series should be totals.

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
    (SELECT unique_id, ds, yhat FROM forecasts)
);

-- With custom column names
SELECT * FROM ts_split_keys(
    (SELECT unique_id, ds, yhat FROM forecasts),
    columns := ['region_id', 'store_id', 'item_id']
);
-- Returns: region_id, store_id, item_id, ds, yhat

-- Custom separator
SELECT * FROM ts_split_keys(
    (SELECT unique_id, ds, yhat FROM forecasts),
    separator := '-'
);

-- Filter to store-level forecasts
SELECT * FROM ts_split_keys(
    (SELECT unique_id, ds, yhat FROM forecasts)
) WHERE id_part_3 = 'AGGREGATED' AND id_part_2 != 'AGGREGATED';
```

---

## Complete Workflow Example

```sql
-- Step 1: Validate separator
SELECT * FROM ts_validate_separator(
    (SELECT region_id, store_id, item_id FROM raw_sales)
);

-- Step 2: Create aggregated time series (supports any number of hierarchy levels)
CREATE TABLE prepared_data AS
SELECT * FROM ts_aggregate_hierarchy(
    (SELECT sale_date, quantity, region_id, store_id, item_id FROM raw_sales),
    MAP{}
);

-- Step 3: Forecast all series (original + aggregated)
CREATE TABLE forecasts AS
SELECT * FROM ts_forecast_by('prepared_data', unique_id, sale_date, quantity,
    'AutoETS', 28, '1d', MAP{'seasonal_period': '7'});

-- Step 4: Split keys for analysis with original column names (ts_split_keys
-- preserves the date/value column names passed in -- ds/yhat here, since that's
-- what ts_forecast_by's output actually calls them)
SELECT *
FROM ts_split_keys(
    (SELECT unique_id, ds, yhat FROM forecasts),
    columns := ['region_id', 'store_id', 'item_id']
)
ORDER BY region_id, store_id, item_id, ds;
```

---

*See also: [Forecasting](07-forecasting.md) | [Cross-Validation](08-cross-validation.md)*
