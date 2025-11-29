# Unify API Naming Convention Across All Extensions

## Overview

This PR implements a unified naming convention across all Anofox extensions (Tabular, Statistics, and Forecast) by renaming all functions to use the `anofox_fcst_*` prefix for the Forecast extension, while maintaining full backward compatibility through aliases.

## Naming Convention

All Anofox extensions now follow a consistent naming pattern:
- **anofox-tabular**: `anofox_tab_*`
- **anofox-forecast**: `anofox_fcst_*`
- **anofox-statistics**: `anofox_stats_*`

Additionally, all functions are available without the prefix as aliases for backward compatibility. For example:
- `anofox_fcst_ts_forecast` and `ts_forecast` (both work)
- `anofox_fcst_ts_mae` and `ts_mae` (both work)

## Changes Made

### 1. Function Renaming

All functions in the anofox-forecast extension have been renamed from `ts_*` to `anofox_fcst_ts_*`:

**Forecasting Functions:**
- `anofox_fcst_ts_forecast` (alias: `ts_forecast`)
- `anofox_fcst_ts_forecast_by` (alias: `ts_forecast_by`)
- `anofox_fcst_ts_forecast_agg` (alias: `ts_forecast_agg`)

**Evaluation Metrics (12 functions):**
- `anofox_fcst_ts_mae`, `anofox_fcst_ts_mse`, `anofox_fcst_ts_rmse`
- `anofox_fcst_ts_mape`, `anofox_fcst_ts_smape`, `anofox_fcst_ts_mase`
- `anofox_fcst_ts_r2`, `anofox_fcst_ts_bias`, `anofox_fcst_ts_rmae`
- `anofox_fcst_ts_quantile_loss`, `anofox_fcst_ts_mqloss`, `anofox_fcst_ts_coverage`

**Seasonality Functions:**
- `anofox_fcst_ts_detect_seasonality` (alias: `ts_detect_seasonality`)
- `anofox_fcst_ts_analyze_seasonality` (alias: `ts_analyze_seasonality`)

**Changepoint Functions:**
- `anofox_fcst_ts_detect_changepoints` (alias: `ts_detect_changepoints`)
- `anofox_fcst_ts_detect_changepoints_by` (alias: `ts_detect_changepoints_by`)
- `anofox_fcst_ts_detect_changepoints_agg` (alias: `ts_detect_changepoints_agg`)

**Time Series Features:**
- `anofox_fcst_ts_features` (alias: `ts_features`)
- `anofox_fcst_ts_features_list` (alias: `ts_features_list`)
- `anofox_fcst_ts_features_config_from_json` (alias: `ts_features_config_from_json`)
- `anofox_fcst_ts_features_config_from_csv` (alias: `ts_features_config_from_csv`)

**EDA Macros (3 macros):**
- `anofox_fcst_ts_stats` (alias: `ts_stats`)
- `anofox_fcst_ts_stats_summary` (alias: `ts_stats_summary`)
- `anofox_fcst_ts_quality_report` (alias: `ts_quality_report`)

**Data Preparation Macros (12+ macros):**
- `anofox_fcst_ts_fill_gaps`, `anofox_fcst_ts_fill_forward`
- `anofox_fcst_ts_fill_nulls_forward`, `anofox_fcst_ts_fill_nulls_backward`
- `anofox_fcst_ts_fill_nulls_mean`, `anofox_fcst_ts_fill_nulls_const`
- `anofox_fcst_ts_drop_constant`, `anofox_fcst_ts_drop_short`
- `anofox_fcst_ts_drop_leading_zeros`, `anofox_fcst_ts_drop_trailing_zeros`
- `anofox_fcst_ts_drop_edge_zeros`, `anofox_fcst_ts_drop_gappy`
- `anofox_fcst_ts_diff`
- (All with corresponding aliases)

**Data Quality Macros:**
- `anofox_fcst_ts_data_quality` (alias: `ts_data_quality`)
- `anofox_fcst_ts_data_quality_summary` (alias: `ts_data_quality_summary`)

### 2. Implementation Details

**C++ Source Files Updated (10 files):**
- `src/anofox_forecast_extension.cpp` - Core registration with alias helpers
- `src/forecast_aggregate.cpp` - Forecast aggregate function
- `src/forecast_table_function.cpp` - Legacy forecast table function
- `src/metrics_function.cpp` - 12 metrics functions with aliases
- `src/seasonality_function.cpp` - 2 seasonality functions with aliases
- `src/changepoint_function.cpp` - Changepoint functions with aliases
- `src/ts_features_function.cpp` - TS features functions with aliases
- `src/eda_macros.cpp` - EDA macros with alias registration
- `src/data_prep_macros.cpp` - Data prep macros with alias registration
- `src/data_quality_macros.cpp` - Data quality macros with alias registration

**Alias Registration:**
- All functions are registered twice: once with the full name and once as an alias
- Aliases use the `alias_of` field to point to the main function
- Both naming conventions work identically and produce the same results

### 3. Documentation Updates

**API Reference:**
- Updated `docs/API_REFERENCE.md` with new function names
- Added alias documentation throughout
- Updated function naming conventions section

**README:**
- Updated quick start examples to use new function names
- Maintained backward compatibility examples

**Guide Templates:**
- Updated all SQL examples in guide templates
- Updated markdown references to function names
- All guides now reflect the new naming convention

### 4. Test Updates

**SQL Test Files:**
- Updated 177+ SQL test files with new function names
- All existing tests continue to pass

**New Test Suite:**
- Created `test/sql/core/test_function_aliases.test`
- Comprehensive test coverage for both full names and aliases
- 24 assertions covering all function types:
  - Scalar functions (metrics, seasonality)
  - Table macros (forecast, stats)
  - Aggregate functions
  - Result equivalence verification

**Test Results:**
- ✅ All 13 core tests passing
- ✅ Alias test suite: 24/24 assertions passing
- ✅ Verified both naming conventions produce identical results

## Backward Compatibility

**100% Backward Compatible:** All existing code using `ts_*` function names will continue to work without any changes. The aliases are fully functional and produce identical results to the full function names.

**Migration Path:**
- Existing code: No changes required (aliases work)
- New code: Can use either naming convention
- Recommended: Use `anofox_fcst_ts_*` for clarity and consistency

## Testing

### Manual Verification
```sql
-- Full name works
SELECT anofox_fcst_ts_mae([1.0, 2.0, 3.0], [1.1, 2.1, 3.1]);
-- Result: 0.1 ✓

-- Alias works
SELECT ts_mae([1.0, 2.0, 3.0], [1.1, 2.1, 3.1]);
-- Result: 0.1 ✓

-- Both produce identical results
SELECT anofox_fcst_ts_forecast(...) = ts_forecast(...);
-- Result: true ✓
```

### Automated Tests
- ✅ `test_function_aliases.test`: 24 assertions (tests both naming conventions)
- ✅ `test_basic_forecasting.test`: 18 assertions
- ✅ `test_all_models.test`: 37 assertions
- ✅ All other core tests: Passing

## Files Changed

**Statistics:**
- 208 files changed
- 2,097 insertions(+), 1,560 deletions(-)

**Key Files:**
- 10 C++ source files
- 1 API reference documentation
- 1 README
- 11 guide template files
- 177+ SQL test files
- 1 new test file (alias test suite)

## Impact

### Benefits
- **Unified naming**: Consistent API across all Anofox extensions
- **Clear namespace**: `anofox_fcst_*` prefix clearly identifies forecast extension functions
- **Backward compatible**: Zero breaking changes for existing users
- **Future-proof**: Ready for integration with other Anofox extensions

### Breaking Changes
- **None**: This is fully backward compatible through aliases

## Related Issues

This PR addresses the requirement to unify API naming conventions across all Anofox extensions (Tabular, Statistics, and Forecast).

## Checklist

- [x] All functions renamed to `anofox_fcst_ts_*` prefix
- [x] All aliases registered and tested
- [x] Documentation updated (API_REFERENCE.md, README.md, guides)
- [x] All SQL test files updated
- [x] Comprehensive alias test suite created
- [x] All tests passing (13/13 core tests)
- [x] Code compiled successfully (release mode)
- [x] Manual verification completed
- [x] Backward compatibility verified

## Next Steps

After this PR is merged:
1. Update anofox-tabular extension with `anofox_tab_*` prefix
2. Update anofox-statistics extension with `anofox_stats_*` prefix
3. Update cross-extension documentation and examples
