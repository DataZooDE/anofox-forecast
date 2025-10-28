# Testing Guide for anofox-forecast Extension

## Overview
This guide explains how to run and validate the test suite for the anofox-forecast DuckDB extension.

## Test Structure

```
test/sql/
├── core/                          # Core functionality regression tests
│   ├── test_all_models.test      # All 31+ models work correctly
│   └── test_arima_conditional.test # ARIMA with conditional Eigen3
├── features/                      # New features tests
│   ├── test_insample_forecast.test # In-sample fitted values
│   └── test_confidence_level.test  # Confidence level functionality
├── eda/                           # Exploratory Data Analysis tests
│   └── test_eda_macros.test      # All EDA table macros
├── data_prep/                     # Data preparation tests
│   └── test_data_prep_macros.test # All data prep table macros
├── integration/                   # Integration tests
│   └── test_complete_workflow.test # Full EDA → Prep → Forecast
└── edge_cases/                    # Edge cases and error handling
    └── test_error_handling.test   # Invalid inputs, errors
```

## Running Tests

### Method 1: Using DuckDB Test Runner (Recommended)

The extension uses DuckDB's standard test framework. Tests are automatically run during the build process.

```bash
# Build and run all tests
cd /home/simonm/projects/ai/anofox-forecast
make test

# Build in debug mode for better error messages
make debug

# Run tests with verbose output
GEN=ninja BUILD_UNITTEST=1 make
```

### Method 2: Manual Test Execution

You can run individual test files manually:

```bash
# Build the extension first
make release

# Load and run a specific test
duckdb << EOF
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
.read test/sql/core/test_all_models.test
EOF
```

### Method 3: Interactive Testing

For debugging specific tests:

```bash
# Start DuckDB
duckdb

# In DuckDB:
D LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
D .read test/sql/core/test_all_models.test
```

## Test Categories Explained

### 1. Core Functionality Tests (`test/sql/core/`)

**Purpose:** Ensure all existing functionality still works after changes

**Key Tests:**
- `test_all_models.test`: Verifies all 31+ forecasting models produce valid output
  - Basic models: Naive, SMA, SeasonalNaive, SES, etc.
  - Holt models: Holt, HoltWinters
  - Theta variants: Theta, OptimizedTheta, Dynamic variants
  - State space: ETS, AutoETS
  - Multiple seasonality: MFLES, MSTL, TBATS
  - Intermittent demand: Croston variants, ADIDA, IMAPA, TSB

- `test_arima_conditional.test`: Tests ARIMA/AutoARIMA models
  - Works when Eigen3 is available
  - Fails gracefully when Eigen3 is not available
  - Other models always work regardless of Eigen3

**Expected Results:** All models should return forecasts of correct length

### 2. New Features Tests (`test/sql/features/`)

**Purpose:** Validate newly implemented features

**Key Tests:**
- `test_insample_forecast.test`: Tests `insample_fitted` field
  - Default behavior (empty when not requested)
  - Correct length when `return_insample=true`
  - Fitted values match model expectations (e.g., Naive: fitted[i] = actual[i-1])
  
- `test_confidence_level.test`: Tests `confidence_level` field
  - Default is 0.90
  - Custom levels work (0.80, 0.95, 0.99)
  - Higher confidence → wider prediction intervals

**Expected Results:** New fields are present and behave correctly

### 3. EDA Tests (`test/sql/eda/`)

**Purpose:** Validate exploratory data analysis macros

**Key Tests:**
- `test_eda_macros.test`: Tests all EDA table macros
  - `TS_STATS`: Basic statistics (mean, std, min, max, quartiles)
  - `TS_QUALITY_REPORT`: Data quality issues (nulls, zeros, constants, gaps)
  - `TS_ANALYZE_ZEROS`: Zero value analysis
  - `TS_DETECT_GAPS`: Missing timestamp detection
  - `TS_CHECK_STATIONARITY`: Stationarity tests
  - `TS_DETECT_OUTLIERS`: Outlier detection
  - `TS_DISTRIBUTION_SUMMARY`: Distribution metrics

**Expected Results:** Macros detect appropriate data quality issues

### 4. Data Preparation Tests (`test/sql/data_prep/`)

**Purpose:** Validate data preparation macros

**Key Tests:**
- `test_data_prep_macros.test`: Tests all data prep table macros
  - `TS_FILL_GAPS`: Fill missing timestamps with interpolation
  - `TS_FILL_NULLS_FORWARD/BACKWARD`: Fill NULL values
  - `TS_REMOVE_OUTLIERS`: Remove or cap outliers
  - `TS_NORMALIZE`: Z-score and min-max normalization
  - `TS_DROP_CONSTANT`: Remove constant series
  - `TS_DROP_ZEROS`: Remove all-zero series

**Expected Results:** Data is appropriately cleaned and transformed

### 5. Integration Tests (`test/sql/integration/`)

**Purpose:** Test complete workflows combining multiple features

**Key Tests:**
- `test_complete_workflow.test`: Full pipeline test
  - Step 1: EDA to identify issues
  - Step 2: Data preparation to fix issues
  - Step 3: Forecasting with clean data
  - Step 4: Model comparison
  - Step 5: Forecast evaluation with metrics

**Expected Results:** Complete workflow produces valid forecasts and metrics

### 6. Edge Cases Tests (`test/sql/edge_cases/`)

**Purpose:** Ensure robust error handling

**Key Tests:**
- `test_error_handling.test`: Tests error conditions
  - Invalid model names
  - Negative/zero horizons
  - Invalid confidence levels
  - Missing required parameters
  - Insufficient data
  - NULL/empty series
  - Extreme values

**Expected Results:** System fails gracefully with clear error messages

## Interpreting Test Results

### Successful Test
```
test/sql/core/test_all_models.test ........................ ok
```

### Failed Test
```
test/sql/core/test_all_models.test ........................ FAILED
```

Check the error message for details. Common issues:
- Missing Eigen3: ARIMA tests may fail (expected on tidy-check)
- Data format: Ensure test data matches expected schema
- Extension not loaded: Ensure `LOAD anofox_forecast;` succeeds

## Continuous Integration

Tests are automatically run in GitHub Actions:

1. **Format Check**: Code style validation
2. **Tidy Check**: Static analysis (Eigen3 optional)
3. **Build & Test**: Full test suite on multiple platforms

### Conditional Compilation Verification

The test suite automatically adapts to Eigen3 availability:

- **With Eigen3**: ARIMA/AutoARIMA tests pass
- **Without Eigen3**: ARIMA/AutoARIMA are unavailable, other tests still pass

To manually verify:

```bash
# Check if ARIMA is available
duckdb << EOF
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';
SELECT * FROM ts_list_models() WHERE model_name LIKE '%ARIMA%';
EOF

# If Eigen3 is present: Shows ARIMA and AutoARIMA
# If Eigen3 is absent: Shows 0 rows
```

## Adding New Tests

When adding new features, follow this pattern:

1. Create a test file in the appropriate category
2. Use the `.test` extension
3. Include header comments:
   ```sql
   # name: test/sql/<category>/<test_name>.test
   # description: Brief description of what is tested
   # group: [anofox_forecast]
   ```

4. Load the extension:
   ```sql
   require anofox_forecast
   statement ok
   LOAD anofox_forecast;
   ```

5. Write test queries with expected results:
   ```sql
   query I
   SELECT len(result.forecast) = 12
   FROM (SELECT TS_FORECAST(value, 'Naive', 12, NULL) AS result FROM test_data);
   ----
   true
   ```

6. Clean up test tables at the end

## Test Coverage

Current test coverage includes:

- ✅ All 31+ forecasting models
- ✅ In-sample forecasts (fitted values)
- ✅ Confidence level specification
- ✅ Prediction intervals
- ✅ All EDA macros (7+ functions)
- ✅ All data prep macros (10+ functions)
- ✅ Table macros (`ts_forecast`, `ts_forecast_by`)
- ✅ Metrics functions (MAE, RMSE, MAPE, etc.)
- ✅ Multi-group forecasting
- ✅ Complete workflows
- ✅ Edge cases and error handling
- ✅ Conditional compilation (ARIMA/Eigen3)

## Performance Testing

For performance regression testing:

```bash
# Time a large-scale test
time duckdb << EOF
LOAD 'build/release/extension/anofox_forecast/anofox_forecast.duckdb_extension';

-- Test with 1000 series, 100 points each
WITH test_data AS (
    SELECT 
        (i / 100) AS series_id,
        i % 100 AS idx,
        random() * 100 AS value
    FROM generate_series(1, 100000) t(i)
)
SELECT series_id, result.*
FROM ts_forecast_by(
    test_data,
    'idx',
    'value',
    'series_id',
    'Naive',
    10,
    {}
);
EOF
```

Benchmark typical operations:
- Simple model (Naive): <1s for 1000 series
- Complex model (AutoETS): <10s for 1000 series
- EDA analysis: <2s for 1000 series
- Data preparation: <3s for 1000 series

## Troubleshooting

### Test Fails with "Extension not found"
```bash
# Rebuild the extension
make clean
make release
```

### Test Fails with "ARIMA not available"
This is expected if Eigen3 is not installed. Either:
1. Install Eigen3: `sudo pacman -S eigen` (Manjaro)
2. Or skip ARIMA tests (they're optional for tidy-check)

### Test Hangs or Times Out
- Check for infinite loops in test data generation
- Reduce test data size for faster iteration
- Use `CTRL+C` to interrupt and check the last query

### Memory Issues
- DuckDB is in-memory by default
- For very large tests, use a persistent database:
  ```bash
  duckdb test.db < test/sql/integration/test_complete_workflow.test
  ```

## Best Practices

1. **Keep tests fast**: Unit tests should run in <1s, integration tests <10s
2. **Test one thing**: Each test should validate a specific behavior
3. **Use descriptive names**: Test file and query names should be self-explanatory
4. **Clean up**: Always drop temporary tables at the end
5. **Document expectations**: Include comments explaining what should happen
6. **Test both success and failure**: Don't just test the happy path

## Reporting Issues

If tests fail unexpectedly:

1. Check the error message and stack trace
2. Run the test manually for more details
3. Verify the extension loaded correctly
4. Check for recent code changes that might affect the test
5. Report with:
   - Test file name
   - Error message
   - DuckDB version
   - Extension version
   - Operating system

## Summary

The test suite provides comprehensive validation of:
- Core functionality (regression testing)
- New features (feature validation)
- Data quality tools (EDA & data prep)
- Complete workflows (integration testing)
- Error handling (robustness)

Run tests regularly to ensure code quality and catch regressions early!

