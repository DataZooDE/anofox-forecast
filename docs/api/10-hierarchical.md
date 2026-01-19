# Hierarchical Time Series

> Multi-key hierarchy functions for aggregated forecasting

## Overview

When working with hierarchical time series data (e.g., region/store/item), you often need to combine multiple ID columns into a single `unique_id` for forecasting functions.

---

## Quick Start

```sql
-- Step 1: Validate separator is safe
SELECT * FROM ts_validate_separator('raw_sales', region_id, store_id, item_id);

-- Step 2: Create hierarchical time series with all aggregation levels
CREATE TABLE prepared_data AS
SELECT * FROM ts_aggregate_hierarchy('raw_sales', sale_date, quantity,
    region_id, store_id, item_id);

-- Step 3: Forecast all series (original + aggregated)
CREATE TABLE forecasts AS
SELECT * FROM ts_forecast_by('prepared_data', unique_id, date_col, value_col,
    'AutoETS', 28, {'seasonal_period': 7});

-- Step 4: Split keys for analysis
SELECT id_part_1 AS region_id, id_part_2 AS store_id, id_part_3 AS item_id,
       date_col AS forecast_date, value_col AS point_forecast
FROM ts_split_keys('forecasts', id, ds, point_forecast);
```

---

## Workflow Summary

1. **Validate separator** - Check if your chosen separator is safe
2. **Combine keys** - Create a single unique_id from multiple columns
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

Combines multiple ID columns into a single `unique_id` column without creating aggregated series.

**Signature:**
```sql
ts_combine_keys(source, date_col, value_col, id_col1, [id_col2], [id_col3], [id_col4], [id_col5], params := MAP{})
```

**Parameters:**
- `source`: Table name (VARCHAR)
- `date_col`: Date/timestamp column
- `value_col`: Value column
- `id_col1-5`: ID columns (1 required, up to 5)
- `params`: MAP with `'separator'` option (default: `'|'`)

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `unique_id` | VARCHAR | Combined ID (e.g., `'EU|STORE001|SKU42'`) |
| `date_col` | (input type) | Date column |
| `value_col` | (input type) | Value column |

**Example:**
```sql
-- Basic combination with 3 columns
SELECT * FROM ts_combine_keys('sales', sale_date, quantity, region_id, store_id, item_id);
-- unique_id: 'EU|STORE001|SKU42'

-- Custom separator
SELECT * FROM ts_combine_keys('sales', sale_date, quantity, region_id, store_id,
    params := {'separator': '-'});
```

---

## Aggregate Hierarchy

### ts_aggregate_hierarchy

Combines ID columns AND creates aggregated series at all hierarchy levels.

**Signature:**
```sql
ts_aggregate_hierarchy(source, date_col, value_col, id_col1, id_col2, id_col3, params := MAP{})
```

**Parameters:**
- `source`: Table name
- `date_col`: Date/timestamp column
- `value_col`: Value column (will be summed at each aggregation level)
- `id_col1-3`: ID columns (broadest to finest granularity)
- `params`: MAP with `'separator'` and `'aggregate_keyword'` options

**Aggregation Levels Created:**

| Level | Pattern | Description |
|-------|---------|-------------|
| 0 | `AGGREGATED|AGGREGATED|AGGREGATED` | Grand total |
| 1 | `EU|AGGREGATED|AGGREGATED` | Per first column |
| 2 | `EU|STORE001|AGGREGATED` | Per first two columns |
| 3 | `EU|STORE001|SKU42` | Original item level |

**Example:**
```sql
-- Create hierarchical time series
SELECT * FROM ts_aggregate_hierarchy('sales', sale_date, quantity,
    region_id, store_id, item_id);

-- Count series at each level
WITH agg AS (
    SELECT * FROM ts_aggregate_hierarchy('sales', sale_date, quantity,
        region_id, store_id, item_id)
)
SELECT
    CASE
        WHEN unique_id = 'AGGREGATED|AGGREGATED|AGGREGATED' THEN 'Grand Total'
        WHEN unique_id LIKE '%|AGGREGATED|AGGREGATED' THEN 'Per Region'
        WHEN unique_id LIKE '%|AGGREGATED' THEN 'Per Store'
        ELSE 'Original Items'
    END AS level,
    COUNT(DISTINCT unique_id) AS n_series
FROM agg
GROUP BY 1;

-- Custom aggregate keyword
SELECT DISTINCT unique_id
FROM ts_aggregate_hierarchy('sales', sale_date, quantity,
    region_id, store_id, item_id,
    params := {'aggregate_keyword': 'TOTAL'})
WHERE unique_id LIKE '%TOTAL%';
```

---

## Split Keys

### ts_split_keys

Splits a combined unique_id back into its original component columns.

**Signature:**
```sql
ts_split_keys(source, id_col, date_col, value_col, separator := '|')
```

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `id_part_1` | VARCHAR | First component |
| `id_part_2` | VARCHAR | Second component |
| `id_part_3` | VARCHAR | Third component |
| `date_col` | (input type) | Date column |
| `value_col` | (input type) | Value column |

**Example:**
```sql
-- Split keys after forecasting
SELECT
    id_part_1 AS region_id,
    id_part_2 AS store_id,
    id_part_3 AS item_id,
    date_col AS forecast_date,
    value_col AS point_forecast
FROM ts_split_keys('forecasts', id, ds, point_forecast);

-- Filter to store-level forecasts
SELECT *
FROM ts_split_keys('forecasts', id, ds, point_forecast)
WHERE id_part_3 = 'AGGREGATED' AND id_part_2 != 'AGGREGATED';
```

---

## Complete Workflow Example

```sql
-- Step 1: Validate separator
SELECT * FROM ts_validate_separator('raw_sales', region_id, store_id, item_id);

-- Step 2: Create aggregated time series
CREATE TABLE prepared_data AS
SELECT * FROM ts_aggregate_hierarchy('raw_sales', sale_date, quantity,
    region_id, store_id, item_id);

-- Step 3: Forecast all series (original + aggregated)
CREATE TABLE forecasts AS
SELECT * FROM ts_forecast_by('prepared_data', unique_id, date_col, value_col,
    'AutoETS', 28, {'seasonal_period': 7});

-- Step 4: Split keys for analysis
SELECT
    id_part_1 AS region_id,
    id_part_2 AS store_id,
    id_part_3 AS item_id,
    date_col AS forecast_date,
    value_col AS point_forecast
FROM ts_split_keys('forecasts', id, ds, point_forecast)
ORDER BY region_id, store_id, item_id, forecast_date;
```

---

*See also: [Forecasting](05-forecasting.md) | [Cross-Validation](06-cross-validation.md)*
