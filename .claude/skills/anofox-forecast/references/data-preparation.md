# Data Preparation Reference

**CRITICAL:** Do NOT chain `_by` table functions in CTEs — returns 0 rows silently. Always `CREATE TABLE` between steps.

## Series Filtering

### ts_drop_constant_by
```sql
ts_drop_constant_by(source VARCHAR, group_col COLUMN, value_col COLUMN) → TABLE
```
Remove series where all values are constant.

### ts_drop_short_by
```sql
ts_drop_short_by(source VARCHAR, group_col COLUMN, min_length INTEGER) → TABLE
```
Remove series shorter than min_length.

### ts_drop_gappy_by
```sql
ts_drop_gappy_by(source VARCHAR, group_col COLUMN, value_col COLUMN, max_gap_ratio DOUBLE) → TABLE
```
Remove series with gap ratio exceeding threshold (e.g., 0.1 = 10%).

### ts_drop_zeros_by
```sql
ts_drop_zeros_by(source VARCHAR, group_col COLUMN, value_col COLUMN) → TABLE
```
Remove all-zero series.

## Edge Cleaning

### ts_drop_leading_zeros_by
```sql
ts_drop_leading_zeros_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

### ts_drop_trailing_zeros_by
```sql
ts_drop_trailing_zeros_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

### ts_drop_edge_zeros_by
```sql
ts_drop_edge_zeros_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```
Removes both leading and trailing zeros.

## Missing Value Imputation

### ts_fill_nulls_const_by
```sql
ts_fill_nulls_const_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, fill_value DOUBLE) → TABLE
```

### ts_fill_nulls_forward_by
```sql
ts_fill_nulls_forward_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```
Last observation carried forward.

### ts_fill_nulls_backward_by
```sql
ts_fill_nulls_backward_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

### ts_fill_nulls_mean_by
```sql
ts_fill_nulls_mean_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN) → TABLE
```

## Gap Filling

### ts_fill_gaps_by
```sql
ts_fill_gaps_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, frequency VARCHAR) → TABLE
```
Insert rows for missing timestamps, filled with NULL. Streaming implementation (15x memory reduction).

Frequency formats: '1d', '1h', '30m', '1w', '1mo', '1q', '1y', '1 day', '1 hour', raw integers.

### ts_fill_forward_by
```sql
ts_fill_forward_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, target_date DATE, frequency VARCHAR) → TABLE
```
Extend series to target date with NULL values.

## Differencing

### ts_diff_by
```sql
ts_diff_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, diff_order INTEGER) → TABLE
```

## Future Value Handling

### ts_fill_unknown_by
```sql
ts_fill_unknown_by(source VARCHAR, group_col COLUMN, date_col COLUMN, value_col COLUMN, cutoff_date DATE, params MAP) → TABLE
```
Params: strategy ('null', 'last_value', 'default'), fill_value (DOUBLE, for 'default' strategy).

### ts_mark_unknown_by
```sql
ts_mark_unknown_by(source VARCHAR, group_col COLUMN, date_col COLUMN, cutoff_date DATE) → TABLE
```
Returns all source columns + is_unknown (BOOLEAN) + last_known_date (TIMESTAMP).

## Timestamp Validation

### ts_validate_timestamps_by
```sql
ts_validate_timestamps_by(source VARCHAR, group_col COLUMN, date_col COLUMN, expected_timestamps DATE[]) → TABLE
```
Returns: group_col, is_valid, n_expected, n_found, n_missing, missing_timestamps[].

### ts_validate_timestamps_summary_by
```sql
ts_validate_timestamps_summary_by(source VARCHAR, group_col COLUMN, date_col COLUMN, expected_timestamps DATE[]) → TABLE
```
Returns: all_valid, n_groups, n_valid_groups, n_invalid_groups, invalid_groups[].

## Standard Pipeline

```sql
-- Step 1: Fill gaps
CREATE TABLE step1 AS
SELECT * FROM ts_fill_gaps_by('raw_data', product_id, date, value, '1d');

-- Step 2: Impute NULLs
CREATE TABLE step2 AS
SELECT * FROM ts_fill_nulls_const_by('step1', product_id, date, value, 0.0);

-- Step 3: Drop short series
CREATE TABLE clean AS
SELECT * FROM ts_drop_short_by('step2', product_id, 20);
```
