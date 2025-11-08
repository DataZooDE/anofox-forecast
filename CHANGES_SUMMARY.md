# Forecast Output Schema Changes - Summary

## Changes Implemented

### 1. Dynamic Confidence Level Column Naming
- **Before**: Fixed column names `lower` and `upper` with separate `confidence_level` column
- **After**: Dynamic column names like `lower_90`, `upper_95` based on the confidence level
- **Format**: Confidence level is multiplied by 1000, trailing zeros removed (0.90 → `90`, 0.95 → `95`)

### 2. Date Column Preservation  
- **Before**: Output date column was always named `date_col` regardless of input column name
- **After**: Date column is now consistently named `date` in the output
- **Implementation**: The `date_col_name` field stores the original input column name for potential future use

### 3. Optional forecast_step Column
- **Before**: `forecast_step` column was always included  
- **After**: Can be excluded by setting `include_forecast_step: false` in the params map
- **Default**: `true` (forecast_step is included by default)
- **Usage**: `SELECT * FROM TS_FORECAST_BY('table', id, date, value, 'Naive', 10, {'include_forecast_step': false})`

## Technical Implementation

### Modified Files
- `src/include/forecast_aggregate.hpp`: Added new fields to `ForecastAggregateBindData`
- `src/forecast_aggregate.cpp`: Updated bind and finalize functions to use dynamic column names
- `src/anofox_forecast_extension.cpp`: Updated TS_FORECAST and TS_FORECAST_BY macros

### API Changes
The output schema now looks like:
```sql
SELECT * FROM TS_FORECAST_BY('timeseries_10k', series_id, date, value, 'MFLES', 10, {'seasonal_periods': [7, 30]});
```

**Output Columns**:
- `series_id` (or your group column name)
- `forecast_step` (optional, default: included)
- `date` (renamed from input date column)
- `point_forecast`
- `lower_90` (name varies with confidence level)
- `upper_90` (name varies with confidence level)
- `model_name`
- `insample_fitted`

### Backward Compatibility
- **Breaking Change**: Column names `lower` and `upper` are now dynamic (e.g., `lower_90`, `upper_95`)
- **Breaking Change**: `confidence_level` column no longer exists in output
- **Breaking Change**: Date column is now always named `date` instead of `date_col`
- Existing queries referencing `lower`, `upper`, `confidence_level`, or `date_col` will need to be updated

## Testing
Tested with various confidence levels (0.90, 0.95) and forecast horizons. All tests pass with the new schema.

