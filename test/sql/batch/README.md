# Batch Processing Consistency Tests

This directory contains SQL tests for verifying batch processing consistency across all time series models.

## Test Files

### test_batch_consistency.test
**Purpose**: Tests basic batch consistency for known-working models

**Models tested**:
- Naive
- SMA (Simple Moving Average)
- SES (Simple Exponential Smoothing)
- ETS (Error-Trend-Seasonality)
- CrostonClassic

**Test data**: 3 synthetic series (constant, trend, seasonal)

**Validates**:
- ✅ Batch (`TS_FORECAST_BY`) matches individual (`ts_forecast`)
- ✅ No negative forecasts for positive-valued series
- ✅ Forecast ranges are reasonable

---

### test_all_models_consistency.test
**Purpose**: Comprehensive test for all models known to work correctly

**Models tested**: 7 models (Naive, SMA, SES, SESOptimized, ETS, CrostonClassic, CrostonOptimized)

**Test data**: 2 series (constant, trend)

**Validates**:
- ✅ Each model maintains batch/individual consistency
- ✅ No negative forecasts across all models

**Usage**: This test should PASS with current implementation. Add more models as bugs are fixed.

---

### test_autoarima_batch_bug.test
**Purpose**: Documents and tests the AutoARIMA batch processing bug

**Status**: ⚠️ **Most tests commented out** - will fail until bug is fixed

**Test phases** (from investigation plan):

#### Phase 1.1: Minimal Reproduction
- Tests 2 constant series (100 and 200)
- Verifies if contamination occurs

#### Phase 1.2: Order Dependency
- Tests forward order [A, B] vs reverse [B, A]
- Determines if processing order matters

#### Phase 1.3: Single Series
- Tests single series in batch mode
- Determines if bug requires multiple series

**How to use**:
1. Uncomment tests as you progress through investigation phases
2. Tests document expected behavior after fix
3. All tests should PASS when bug is fixed

---

## Running Tests

### Run all batch tests
```bash
make test_debug
# Or filter to batch tests only
./build/debug/test/unittest "batch/*"
```

### Run specific test
```bash
./build/debug/test/unittest "test/sql/batch/test_batch_consistency.test"
```

### Expected results (current state)
- ✅ `test_batch_consistency.test` - **PASS** (11 assertions)
- ✅ `test_all_models_consistency.test` - **PASS** (8 assertions)
- ⚠️ `test_autoarima_batch_bug.test` - **PASS** (no active tests yet - AutoARIMA tests commented out)

---

## Test Development Guidelines

### Adding new model tests

1. **Test in Python first** to verify model works individually:
```python
# Quick validation
import duckdb
con = duckdb.connect()
con.execute("LOAD anofox_forecast")
con.execute("CREATE TABLE test AS SELECT ...")
result = con.execute("SELECT * FROM ts_forecast(test, ...)").fetchdf()
print(result)
```

2. **Add to SQL test** once model is confirmed working:
```sql
# Add to test_all_models_consistency.test
statement ok
CREATE OR REPLACE TABLE test_batch AS
SELECT id, point_forecast
FROM TS_FORECAST_BY(comprehensive_test, id, date, value, 'NewModel', 14, {...});

# ... test logic ...

query I
SELECT COUNT(*) = 0 FROM (
    -- Compare batch vs individual
) WHERE ABS(batch_fcst - ind_fcst) > 0.01;
----
true
```

3. **Update documentation** in this README with model status

### Test pattern template

```sql
# Test Model X: ModelName (STATUS)
statement ok
CREATE OR REPLACE TABLE test_batch AS
SELECT id, point_forecast
FROM TS_FORECAST_BY(test_data, id, date, value, 'ModelName', horizon, {params});

statement ok
CREATE OR REPLACE TABLE test_individual AS
SELECT 'Series1' AS id, point_forecast
FROM ts_forecast((SELECT date, value FROM test_data WHERE id = 'Series1'),
                  date, value, 'ModelName', horizon, {params})
UNION ALL
SELECT 'Series2' AS id, point_forecast
FROM ts_forecast((SELECT date, value FROM test_data WHERE id = 'Series2'),
                  date, value, 'ModelName', horizon, {params});

# Verify consistency (tolerance 0.01)
query I
SELECT COUNT(*) = 0 FROM (
    SELECT b.id, b.point_forecast AS batch_fcst, i.point_forecast AS ind_fcst,
           row_number() OVER (PARTITION BY b.id) AS rn
    FROM test_batch b
    JOIN test_individual i ON b.id = i.id
) WHERE ABS(batch_fcst - ind_fcst) > 0.01;
----
true

# Verify no negative forecasts
query I
SELECT COUNT(*) = 0 FROM test_batch WHERE point_forecast < 0;
----
true
```

---

## Model Status Matrix

| Model | Batch Consistency | Test Coverage | Notes |
|-------|------------------|---------------|-------|
| **Naive** | ✅ PASS | ✅ Complete | Working correctly |
| **SMA** | ✅ PASS | ✅ Complete | Working correctly |
| **SES** | ✅ PASS | ✅ Complete | Working correctly |
| **SESOptimized** | ✅ PASS | ✅ Complete | Working correctly |
| **ETS** | ✅ PASS | ✅ Complete | Working correctly |
| **CrostonClassic** | ✅ PASS | ✅ Complete | Working correctly |
| **CrostonOptimized** | ✅ PASS | ✅ Complete | Working correctly |
| **CrostonSBA** | ✅ PASS | ⚠️ Partial | Not in test suite yet |
| **ADIDA** | ✅ PASS | ⚠️ Partial | Not in test suite yet |
| **IMAPA** | ✅ PASS | ⚠️ Partial | Not in test suite yet |
| **TSB** | ✅ PASS | ⚠️ Partial | Not in test suite yet |
| **SeasonalNaive** | ❌ FAIL | ⚠️ Documented | Ordering/precision issue |
| **RandomWalkWithDrift** | ❌ FAIL | ⚠️ Documented | Ordering/precision issue |
| **Holt** | ❌ FAIL | ⚠️ Documented | Ordering/precision issue |
| **HoltWinters** | ❌ FAIL | ⚠️ Documented | Ordering/precision issue |
| **Theta** | ❌ FAIL | ⚠️ Documented | Ordering/precision issue |
| **ARIMA** | ❌ FAIL | ⚠️ Documented | Minor mismatches |
| **AutoARIMA** | ❌ FAIL | ✅ Complete | Critical bug - negative forecasts |
| **AutoETS** | ❌ FAIL | ⚠️ Partial | Works on M4, fails on synthetic |
| **MFLES** | ❌ FAIL | ⚠️ Documented | Ordering/precision issue |
| **AutoMFLES** | ❌ FAIL | ⚠️ Documented | Ordering/precision issue |
| **MSTL** | ❌ FAIL | ⚠️ Documented | Ordering/precision issue |
| **AutoMSTL** | ❌ FAIL | ⚠️ Documented | Ordering/precision issue |
| **TBATS** | ❌ FAIL | ⚠️ Documented | Ordering/precision issue |
| **AutoTBATS** | ❌ FAIL | ⚠️ Documented | Ordering/precision issue |

---

## Investigation Progress

### Phase 1: Isolate Failure ⏳
- [ ] Step 1.1: Minimal 2-series reproduction
- [ ] Step 1.2: Order dependency testing
- [ ] Step 1.3: Single series in batch mode

### Phase 2: Examine Code ⏳
- [ ] Step 2.1: Review model creation
- [ ] Step 2.2: Trace ARIMA implementation
- [ ] Step 2.3: Inspect differencing logic

### Phase 3: Instrument & Debug ⏳
- [ ] Step 3.1: Add logging to model creation
- [ ] Step 3.2: Add logging to fit/predict

### Phase 4: Hypothesis Testing ⏳
- [ ] Step 4.1: Test static variable hypothesis
- [ ] Step 4.2: Test differencing state hypothesis

### Phase 5: Implement Fix ⏳
- [ ] Step 5.1: Apply confirmed fix
- [ ] Step 5.2: Verify with all tests
- [ ] Step 5.3: Add regression tests

---

## References

- Investigation plan: `benchmark/arima_benchmark/BUG_INVESTIGATION_PLAN.md`
- Test results: `benchmark/arima_benchmark/COMPREHENSIVE_TEST_RESULTS.md`
- Bug report: `benchmark/arima_benchmark/CRITICAL_BUG_REPORT.md`
