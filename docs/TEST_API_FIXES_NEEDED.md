# Test API Fixes Needed

## Problem

The test files were written assuming incorrect function signatures. The actual extension API is different from what was assumed in the tests.

## Actual Extension API

### Forecast Functions
- **Aggregate**: `TS_FORECAST_AGG(date_col, target_col, method, horizon, params)`
  - Used in GROUP BY queries
  - Returns a struct with forecast results
- **Table Macro**: `ts_forecast(table_name, date_col, target_col, method, horizon, params)`
  - Returns a table with forecasts
- **Grouped Table Macro**: `ts_forecast_by(table_name, group_col, date_col, target_col, method, horizon, params)`
  - Returns table with forecasts for each group

### EDA Macros
All EDA macros have signature: `(table_name, group_cols, date_col, value_col)`
- `ts_stats(table_name, group_cols, date_col, value_col)`
- `ts_quality_report(table_name, group_cols, date_col, value_col)`
- `ts_analyze_zeros(table_name, group_cols, date_col, value_col)`
- `ts_detect_gaps(table_name, group_cols, date_col, value_col)`
- `ts_check_stationarity(table_name, group_cols, date_col, value_col)`
- `ts_detect_outliers(table_name, group_cols, date_col, value_col)`
- `ts_distribution_summary(table_name, group_cols, date_col, value_col)`

### Data Prep Macros
All data prep macros have signature: `(table_name, group_cols, date_col, value_col)`
- `ts_fill_gaps(table_name, group_cols, date_col, value_col)`
- `ts_fill_nulls_forward(table_name, group_cols, date_col, value_col)`
- `ts_fill_nulls_backward(table_name, group_cols, date_col, value_col)`
- `ts_remove_outliers(table_name, group_cols, date_col, value_col)`
- `ts_normalize(table_name, group_cols, date_col, value_col)`
- `ts_drop_constant(table_name, group_cols, date_col, value_col)`
- `ts_drop_zeros(table_name, group_cols, date_col, value_col)`

## What Was Wrong in Tests

### 1. Wrong Function Name
Tests used: `TS_FORECAST(value, 'Naive', 5, NULL)`
Should be: `TS_FORECAST_AGG(date_col, value, 'Naive', 5, NULL)`

### 2. Wrong Parameter Order for EDA/Data Prep
Tests used: `TS_STATS('table', 'date', 'value', 'group')`
Should be: `TS_STATS('table', 'group', 'date', 'value')`

### 3. Missing Function
Tests used: `ts_list_models()` - This function doesn't exist

### 4. Wrong Usage Context
Tests tried to use table macros as scalar functions in SELECT:
```sql
-- WRONG:
SELECT (SELECT TS_FORECAST(...) FROM data) AS result

-- CORRECT:
SELECT * FROM ts_forecast(data, 'date', 'value', 'Naive', 5, NULL)
```

## Working Example

See `test/sql/simple_smoke_test.test` for a working example.

```sql
-- Aggregate usage
SELECT TS_FORECAST_AGG(date_col, value, 'Naive', 5, NULL) AS result
FROM test_data;

-- Table macro usage  
SELECT * FROM ts_forecast(test_data, 'date_col', 'value', 'Naive', 5, NULL);

-- Grouped forecast
SELECT * FROM ts_forecast_by(test_data, 'series_id', 'date_col', 'value', 'Naive', 5, NULL);

-- EDA usage
SELECT * FROM ts_stats(test_data, 'series_id', 'date', 'value');
```

## Fixes Needed

All test files in `test/sql/` need to be rewritten:
1. `test/sql/core/test_all_models.test` - Use TS_FORECAST_AGG
2. `test/sql/features/test_insample_forecast.test` - Use TS_FORECAST_AGG
3. `test/sql/features/test_confidence_level.test` - Use TS_FORECAST_AGG
4. `test/sql/eda/test_eda_macros.test` - Fix parameter order
5. `test/sql/data_prep/test_data_prep_macros.test` - Fix parameter order
6. `test/sql/integration/test_complete_workflow.test` - Fix all calls
7. `test/sql/edge_cases/test_error_handling.test` - Use TS_FORECAST_AGG
8. `test/sql/core/test_arima_conditional.test` - Remove ts_list_models, use TS_FORECAST_AGG

## Current Status

- ✅ `simple_smoke_test.test` - Working
- ❌ All other tests - Need API fixes

## Next Steps

1. Update all test files to use correct API
2. Remove dependency on non-existent `ts_list_models()` function
3. Test against actual extension binary
4. Update TESTING_GUIDE.md with correct examples

