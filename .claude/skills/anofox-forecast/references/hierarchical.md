# Hierarchical Time Series Reference

Combine multiple ID columns into a single unique_id for forecasting, aggregate at all hierarchy levels, and split back.

## ts_validate_separator

Check if separator char conflicts with data values.

```sql
ts_validate_separator(
    (SELECT id_col1, id_col2, ... FROM source),
    separator := '|'  -- optional, default '|'
) → TABLE
```

Returns: separator (VARCHAR), is_valid (BOOLEAN), n_conflicts (INTEGER), conflicting_values (VARCHAR[]), message (VARCHAR)

## ts_combine_keys

Combine ID columns into single unique_id. No aggregation.

```sql
ts_combine_keys(
    (SELECT date_col, value_col, id_col1, id_col2, ... FROM source),
    MAP{}  -- optional: {'separator': '|'}
) → TABLE
```

Input columns: 1st=date, 2nd=value, 3rd+=ID columns (any number).

Returns: unique_id (VARCHAR), date_col (preserved), value_col (preserved)

Example: `'EU|STORE001|SKU42'`

## ts_aggregate_hierarchy

Combine keys AND create aggregated series at all levels.

```sql
ts_aggregate_hierarchy(
    (SELECT date_col, value_col, id_col1, id_col2, ... FROM source),
    MAP{}  -- optional: {'separator': '|', 'aggregate_keyword': 'AGGREGATED'}
) → TABLE
```

Input: 1st=date, 2nd=value (summed at each level), 3rd+=ID columns (broadest to finest).

Returns: unique_id (VARCHAR), date (preserved), value (DOUBLE, preserved name)

For N ID columns, creates N+1 levels:
| Level | Pattern (3 cols) | Description |
|-------|-----------------|-------------|
| 0 | AGGREGATED\|AGGREGATED\|AGGREGATED | Grand total |
| 1 | EU\|AGGREGATED\|AGGREGATED | Per first column |
| 2 | EU\|STORE001\|AGGREGATED | Per first two columns |
| 3 | EU\|STORE001\|SKU42 | Original level |

## ts_split_keys

Split combined unique_id back into component columns.

```sql
ts_split_keys(
    (SELECT unique_id, date_col, value_col FROM source),
    separator := '|',                          -- optional
    columns := ['region', 'store', 'item']     -- optional
) → TABLE
```

Input: 1st=unique_id, 2nd=date, 3rd=value. Auto-detects number of parts.

Returns: id_part_1/custom_name, id_part_2/custom_name, ..., date_col, value_col

## Complete Workflow

```sql
-- 1. Validate separator
SELECT * FROM ts_validate_separator(
    (SELECT region_id, store_id, item_id FROM raw_sales));

-- 2. Create aggregated hierarchy
CREATE TABLE prepared AS
SELECT * FROM ts_aggregate_hierarchy(
    (SELECT sale_date, quantity, region_id, store_id, item_id FROM raw_sales), MAP{});

-- 3. Forecast all levels
CREATE TABLE forecasts AS
SELECT * FROM ts_forecast_by('prepared', unique_id, sale_date, quantity,
    'AutoETS', 28, MAP{'seasonal_period': '7'});

-- 4. Split back for analysis
SELECT * FROM ts_split_keys(
    (SELECT unique_id, sale_date, quantity FROM forecasts),
    columns := ['region_id', 'store_id', 'item_id']);
```
