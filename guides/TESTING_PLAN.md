# Comprehensive Testing Plan for anofox-forecast Extension

## Overview
This document outlines the testing strategy to validate all functionality and ensure no regressions were introduced by recent changes.

## Recent Changes to Validate
1. **In-sample forecasts** - Added `insample_fitted` field to aggregate return
2. **Confidence level** - Added `confidence_level` field to aggregate return  
3. **EDA macros** - Added SQL table macros for exploratory data analysis
4. **Data preparation macros** - Added SQL table macros for data preparation
5. **Eigen3 optional compilation** - Made ARIMA models conditional on Eigen3
6. **Code formatting** - Applied clang-format to all source files

## Testing Strategy

### 1. Core Functionality Regression Tests

#### 1.1 All Models Still Work
Test that all 31+ forecasting models can be created and produce forecasts:
- Basic models: Naive, SMA, SeasonalNaive, SES, etc.
- Holt models: Holt, HoltWinters
- Theta variants: Theta, OptimizedTheta, DynamicTheta, DynamicOptimizedTheta
- Seasonal models: SeasonalES, SeasonalESOptimized, SeasonalWindowAverage
- ARIMA models: ARIMA, AutoARIMA (when Eigen3 available)
- State space: ETS, AutoETS
- Multiple seasonality: MFLES, AutoMFLES, MSTL, AutoMSTL, TBATS, AutoTBATS
- Intermittent demand: CrostonClassic, CrostonOptimized, CrostonSBA, ADIDA, IMAPA, TSB

**Test Approach:**
```sql
-- For each model, verify it returns forecasts
SELECT TS_FORECAST(
    value, 
    'ModelName', 
    12,  -- horizon
    {}   -- params
) FROM test_data;
```

#### 1.2 Forecast Aggregate Structure
Verify the aggregate returns the correct structure with new fields:
- `forecast` (LIST(DOUBLE))
- `lower` (LIST(DOUBLE))
- `upper` (LIST(DOUBLE))
- `insample_fitted` (LIST(DOUBLE)) - NEW
- `confidence_level` (DOUBLE) - NEW
- `model_info` (STRUCT)

#### 1.3 Table Macros Still Work
- `ts_forecast` macro
- `ts_forecast_by` macro

### 2. New Features Tests

#### 2.1 In-Sample Fitted Values
Test that `insample_fitted` returns correct values:

```sql
-- Test with return_insample = true
SELECT TS_FORECAST(
    value,
    'Naive',
    5,
    {'return_insample': true}
) AS result
FROM (VALUES (100.0), (102.0), (105.0), (103.0)) t(value);

-- Verify insample_fitted has correct length (training data length)
-- Verify fitted values make sense for the model
```

**Expected behaviors by model type:**
- Naive: fitted[i] = actual[i-1]
- SMA: fitted[i] = average of previous window
- SES: fitted[i] = exponentially smoothed value
- ETS: fitted values from state space model
- ARIMA: fitted values from ARIMA model (if Eigen3 available)

#### 2.2 Confidence Level
Test that `confidence_level` is correctly returned:

```sql
-- Test default confidence level (0.90)
SELECT result.confidence_level 
FROM (
    SELECT TS_FORECAST(value, 'Naive', 5, NULL) AS result
    FROM test_data
);
-- Expected: 0.90

-- Test custom confidence level
SELECT result.confidence_level
FROM (
    SELECT TS_FORECAST(value, 'Naive', 5, {'confidence_level': 0.95}) AS result
    FROM test_data
);
-- Expected: 0.95
```

#### 2.3 Prediction Intervals Calibration
Test that prediction intervals match the specified confidence level:

```sql
-- Generate forecasts with known distribution
-- Check coverage using TS_COVERAGE metric
SELECT TS_COVERAGE(
    actual_values,
    lower_bounds,
    upper_bounds
) AS coverage
FROM forecast_results;
-- Expected: ~0.90 for 90% confidence level
```

### 3. EDA Macros Tests

Test all new EDA table macros:

#### 3.1 TS_STATS
```sql
SELECT * FROM TS_STATS('test_table', 'date_col', 'value_col', 'id_col');
-- Verify returns: count, mean, std, min, max, median, q25, q75
```

#### 3.2 TS_QUALITY_REPORT
```sql
SELECT * FROM TS_QUALITY_REPORT('test_table', 'date_col', 'value_col', 'id_col');
-- Verify detects: nulls, zeros, constants, gaps
```

#### 3.3 TS_ANALYZE_ZEROS
```sql
SELECT * FROM TS_ANALYZE_ZEROS('test_table', 'date_col', 'value_col', 'id_col');
-- Verify counts zero runs
```

#### 3.4 TS_DETECT_GAPS
```sql
SELECT * FROM TS_DETECT_GAPS('test_table', 'date_col', 'value_col', 'id_col', 'DAY');
-- Verify detects missing timestamps
```

#### 3.5 TS_CHECK_STATIONARITY
```sql
SELECT * FROM TS_CHECK_STATIONARITY('test_table', 'date_col', 'value_col', 'id_col');
-- Verify returns statistical measures
```

#### 3.6 TS_DETECT_OUTLIERS
```sql
SELECT * FROM TS_DETECT_OUTLIERS('test_table', 'date_col', 'value_col', 'id_col', 3.0);
-- Verify flags outliers based on threshold
```

#### 3.7 TS_DISTRIBUTION_SUMMARY
```sql
SELECT * FROM TS_DISTRIBUTION_SUMMARY('test_table', 'date_col', 'value_col', 'id_col');
-- Verify returns distribution metrics
```

### 4. Data Preparation Macros Tests

Test all new data preparation table macros:

#### 4.1 TS_FILL_GAPS
```sql
SELECT * FROM TS_FILL_GAPS('test_table', 'date_col', 'value_col', 'id_col', 'DAY', 'linear');
-- Verify fills missing timestamps with interpolated values
```

#### 4.2 TS_FILL_NULLS_FORWARD / BACKWARD
```sql
SELECT * FROM TS_FILL_NULLS_FORWARD('test_table', 'date_col', 'value_col', 'id_col');
SELECT * FROM TS_FILL_NULLS_BACKWARD('test_table', 'date_col', 'value_col', 'id_col');
-- Verify fills NULL values appropriately
```

#### 4.3 TS_REMOVE_OUTLIERS
```sql
SELECT * FROM TS_REMOVE_OUTLIERS('test_table', 'date_col', 'value_col', 'id_col', 3.0, 'remove');
-- Verify removes or caps outliers
```

#### 4.4 TS_NORMALIZE / TS_DENORMALIZE
```sql
WITH normalized AS (
    SELECT * FROM TS_NORMALIZE('test_table', 'date_col', 'value_col', 'id_col', 'zscore')
)
SELECT * FROM TS_DENORMALIZE(normalized, ...);
-- Verify round-trip transformation
```

#### 4.5 TS_DROP_CONSTANT / TS_DROP_ZEROS
```sql
SELECT * FROM TS_DROP_CONSTANT('test_table', 'date_col', 'value_col', 'id_col');
SELECT * FROM TS_DROP_ZEROS('test_table', 'date_col', 'value_col', 'id_col');
-- Verify filters out constant/zero series
```

### 5. Integration Tests

#### 5.1 Complete EDA → Data Prep → Forecast Workflow
```sql
-- Step 1: Analyze data quality
WITH quality AS (
    SELECT * FROM TS_QUALITY_REPORT('raw_data', 'date', 'value', 'series_id')
),
-- Step 2: Fill gaps and nulls
cleaned AS (
    SELECT * FROM TS_FILL_GAPS('raw_data', 'date', 'value', 'series_id', 'DAY', 'linear')
),
filled AS (
    SELECT * FROM TS_FILL_NULLS_FORWARD(cleaned, 'date', 'value', 'series_id')
),
-- Step 3: Remove outliers
prepared AS (
    SELECT * FROM TS_REMOVE_OUTLIERS(filled, 'date', 'value', 'series_id', 3.0, 'cap')
)
-- Step 4: Forecast
SELECT 
    series_id,
    result.*
FROM ts_forecast_by(
    prepared,
    'date',
    'value',
    'series_id',
    'AutoETS',
    12,
    {'return_insample': true}
) AS result;
```

#### 5.2 Multiple Groups Forecasting
```sql
-- Test forecasting with many groups
SELECT series_id, result.* 
FROM ts_forecast_by(
    large_dataset,
    'date',
    'value', 
    'series_id',
    'Naive',
    12,
    {}
);
-- Verify all groups get forecasts
```

#### 5.3 Metrics Calculation
```sql
-- Test all 12 metrics work
SELECT 
    TS_MAE(actual, forecast) AS mae,
    TS_RMSE(actual, forecast) AS rmse,
    TS_MAPE(actual, forecast) AS mape,
    TS_SMAPE(actual, forecast) AS smape,
    TS_MASE(actual, forecast, seasonal_period) AS mase,
    TS_COVERAGE(actual, lower, upper) AS coverage
FROM forecast_validation;
```

### 6. Conditional Compilation Tests

#### 6.1 With Eigen3 Available
```sql
-- These should work
SELECT TS_FORECAST(value, 'ARIMA', 5, {'p': 1, 'd': 1, 'q': 1});
SELECT TS_FORECAST(value, 'AutoARIMA', 5, {'seasonal_period': 12});

-- Verify ARIMA models appear in supported list
SELECT * FROM ts_list_models() WHERE model_name LIKE '%ARIMA%';
```

#### 6.2 Without Eigen3 Available
```sql
-- These should fail gracefully with clear error message
SELECT TS_FORECAST(value, 'ARIMA', 5, {'p': 1, 'd': 1, 'q': 1});
-- Expected error: "Unknown model: 'ARIMA'"

SELECT TS_FORECAST(value, 'AutoARIMA', 5, NULL);
-- Expected error: "Unknown model: 'AutoARIMA'"

-- Verify ARIMA models don't appear in supported list
SELECT * FROM ts_list_models() WHERE model_name LIKE '%ARIMA%';
-- Expected: 0 rows

-- Verify other models still work
SELECT TS_FORECAST(value, 'Naive', 5, NULL);
SELECT TS_FORECAST(value, 'AutoETS', 5, {'seasonal_period': 12});
-- Expected: Success
```

### 7. Edge Cases & Error Handling

#### 7.1 Empty Series
```sql
SELECT TS_FORECAST(value, 'Naive', 5, NULL) 
FROM (SELECT NULL::DOUBLE AS value WHERE FALSE);
-- Expected: Appropriate error
```

#### 7.2 Single Value Series
```sql
SELECT TS_FORECAST(value, 'Naive', 5, NULL)
FROM (VALUES (100.0)) t(value);
-- Expected: Appropriate error or default behavior
```

#### 7.3 Series with All NULLs
```sql
SELECT TS_FORECAST(value, 'Naive', 5, NULL)
FROM (VALUES (NULL), (NULL), (NULL)) t(value);
-- Expected: Appropriate error
```

#### 7.4 Invalid Parameters
```sql
-- Negative horizon
SELECT TS_FORECAST(value, 'Naive', -5, NULL);
-- Expected: Error

-- Invalid confidence level
SELECT TS_FORECAST(value, 'Naive', 5, {'confidence_level': 1.5});
-- Expected: Error

-- Invalid model name
SELECT TS_FORECAST(value, 'NonExistentModel', 5, NULL);
-- Expected: Clear error message
```

#### 7.5 Missing Required Parameters
```sql
-- SeasonalNaive without seasonal_period
SELECT TS_FORECAST(value, 'SeasonalNaive', 5, NULL);
-- Expected: Error about missing seasonal_period

-- HoltWinters without seasonal_period
SELECT TS_FORECAST(value, 'HoltWinters', 5, NULL);
-- Expected: Error about missing seasonal_period
```

### 8. Performance Tests

#### 8.1 Large Series
```sql
-- Test with 10,000 data points
SELECT TS_FORECAST(value, 'Naive', 100, NULL)
FROM generate_series(1, 10000) t(idx)
CROSS JOIN (VALUES (random() * 100)) v(value);
```

#### 8.2 Many Groups
```sql
-- Test with 1,000 groups
SELECT series_id, result.*
FROM ts_forecast_by(
    (SELECT 
        (i / 100)::INTEGER AS series_id,
        i % 100 AS idx,
        random() * 100 AS value
     FROM generate_series(1, 100000) t(i)),
    'idx',
    'value',
    'series_id',
    'Naive',
    10,
    {}
);
```

### 9. Backwards Compatibility Tests

#### 9.1 Existing Queries Still Work
```sql
-- Old-style aggregate call (no new parameters)
SELECT TS_FORECAST(value, 'Naive', 5, NULL)
FROM test_data;
-- Expected: Success, insample_fitted is empty, confidence_level is 0.90

-- Old-style table macro call
SELECT * FROM ts_forecast(
    test_data,
    'date',
    'value',
    'Naive',
    5,
    {}
);
-- Expected: Success, new columns included
```

## Test Implementation

### Test File Organization
```
test/
├── sql/
│   ├── core/
│   │   ├── test_all_models.test
│   │   ├── test_aggregate_structure.test
│   │   └── test_table_macros.test
│   ├── features/
│   │   ├── test_insample_forecast.test
│   │   ├── test_confidence_level.test
│   │   └── test_prediction_intervals.test
│   ├── eda/
│   │   ├── test_ts_stats.test
│   │   ├── test_ts_quality_report.test
│   │   ├── test_ts_analyze_zeros.test
│   │   ├── test_ts_detect_gaps.test
│   │   ├── test_ts_check_stationarity.test
│   │   ├── test_ts_detect_outliers.test
│   │   └── test_ts_distribution_summary.test
│   ├── data_prep/
│   │   ├── test_ts_fill_gaps.test
│   │   ├── test_ts_fill_nulls.test
│   │   ├── test_ts_remove_outliers.test
│   │   ├── test_ts_normalize.test
│   │   └── test_ts_drop_filters.test
│   ├── integration/
│   │   ├── test_complete_workflow.test
│   │   ├── test_multiple_groups.test
│   │   └── test_metrics.test
│   ├── edge_cases/
│   │   ├── test_empty_series.test
│   │   ├── test_single_value.test
│   │   ├── test_nulls.test
│   │   └── test_invalid_params.test
│   └── performance/
│       ├── test_large_series.test
│       └── test_many_groups.test
```

### Running Tests
```bash
# Run all tests
make test

# Run specific test suite
duckdb < test/sql/core/test_all_models.test

# Run with extension loaded
duckdb -c "LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension'; .read test/sql/core/test_all_models.test"
```

## Success Criteria

All tests must:
1. ✅ Pass without errors
2. ✅ Return expected results
3. ✅ Complete in reasonable time (<1s for unit tests, <10s for integration tests)
4. ✅ Provide clear error messages on failure

## Continuous Integration

Tests should be run:
- On every commit
- Before merging PRs
- On multiple platforms (Linux, macOS, Windows)
- With and without Eigen3 (to test conditional compilation)

## Test Data

Use a variety of test datasets:
1. **Synthetic data**: Known patterns (linear, seasonal, random)
2. **Real-world data**: AirPassengers, M4 competition samples
3. **Edge cases**: Empty, single value, all NULLs, all constants
4. **Large scale**: 10K+ points, 1000+ groups

