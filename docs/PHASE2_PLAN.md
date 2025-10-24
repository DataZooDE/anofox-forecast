# Phase 2 Implementation Plan

## Overview

This document outlines the plan for extending the anofox_forecast extension from the current Phase 1 (basic functionality) to Phase 2 (full-featured forecasting system).

## Current Status (Phase 1 Complete)

✅ Extension builds successfully  
✅ FORECAST table function working  
✅ Three baseline models: SMA, Naive, SeasonalNaive  
✅ Comprehensive error handling  
✅ Test suite passing  
✅ Documentation complete  

## Phase 2 Goals

### 1. Full Model Support

**Add remaining 30+ models from anofox-time:**

**Exponential Smoothing:**
- SES (Simple Exponential Smoothing)
- Holt (Double Exponential Smoothing)
- HoltWinters (Triple Exponential Smoothing)
- ETS (Error-Trend-Season framework)
- AutoETS (Automatic ETS model selection)

**ARIMA Family:**
- ARIMA
- AutoARIMA
- SARIMA (Seasonal ARIMA)

**Theta Methods:**
- Theta
- OptimizedTheta
- DynamicTheta
- DynamicOptimizedTheta

**Advanced Methods:**
- MFLES (Multiple Frequency-Level Exponential Smoothing)
- AutoMFLES
- MSTL (Multiple Seasonal-Trend decomposition using Loess)
- AutoMSTL
- TBATS (Trigonometric, Box-Cox, ARIMA, Trend, Seasonal)
- AutoTBATS

**Intermittent Demand:**
- Croston (Classic, Optimized, SBA)
- TSB (Teunter-Syntetos-Babai)
- ADIDA
- IMAPA

**Other:**
- RandomWalkDrift
- SeasonalWindowAverage
- DTW (Dynamic Time Warping)
- Ensemble

**Implementation Steps:**
1. Add model headers to `anofox_time_wrapper.hpp`
2. Create factory methods in `AnofoxTimeWrapper`
3. Add model names to `ModelFactory::GetSupportedModels()`
4. Implement parameter extraction for each model
5. Update CMakeLists.txt to include required source files
6. Add tests for each model
7. Document parameters and usage

### 2. STRUCT Parameter Support

**Current Issue:** Function signature shows `STRUCT()` instead of accepting any STRUCT.

**Solution:**
- Use named_parameters from `TableFunctionBindInput`
- Update bind function to extract parameters dynamically
- Support syntax like: `window := 5, seasonal_period := 12`

**Example Usage After Fix:**
```sql
SELECT * FROM FORECAST('date', 'sales', 'SMA', 12, {'window': 3});
SELECT * FROM FORECAST('date', 'sales', 'SeasonalNaive', 12, {'seasonal_period': 4});
SELECT * FROM FORECAST('date', 'sales', 'ETS', 12, {
    'error': 'additive',
    'trend': 'additive', 
    'season': 'multiplicative',
    'season_length': 12
});
```

**Implementation:**
```cpp
// In ForecastBind:
for (auto &kv : input.named_parameters) {
    // Extract named parameters from struct
    if (kv.first == "window") {
        params_map["window"] = kv.second.GetValue<int>();
    }
    // ... handle other parameters
}
```

### 3. Proper Table Input (not column names)

**Current Issue:** Function takes column names as strings, not actual table data.

**Solution:** Implement proper table-in-out function that accepts table data and works with GROUP BY.

**Design:**
```sql
-- Correct API (as per design doc):
SELECT 
    product_id,
    forecast_step,
    point_forecast
FROM FORECAST(
    sales,              -- TABLE, not string
    'date',
    'sales',
    'Theta',
    12,
    {'seasonal_period': 12}
)
GROUP BY product_id;
```

**Implementation Changes:**
1. Change first parameter from VARCHAR to table reference
2. Implement table scan in ForecastFunction
3. Accumulate data per group
4. Process each group when complete

### 4. Proper Prediction Intervals

**Current:** Simplified intervals (±10% of point forecast)

**Needed:** Statistical prediction intervals from models

**Implementation:**
- Check if model supports confidence intervals
- For Naive: use residual standard deviation
- For ARIMA: use model's predictWithConfidence()
- For others: compute from residuals

**Code:**
```cpp
if (forecast.lower.has_value() && forecast.upper.has_value()) {
    output.data[2].SetValue(i, Value::DOUBLE((*forecast.lower)[0][forecast_idx]));
    output.data[3].SetValue(i, Value::DOUBLE((*forecast.upper)[0][forecast_idx]));
} else {
    // Compute from residuals
    double std_err = computeStdError(model.get());
    double margin = 1.96 * std_err; // 95% CI
    output.data[2].SetValue(i, Value::DOUBLE(point - margin));
    output.data[3].SetValue(i, Value::DOUBLE(point + margin));
}
```

### 5. Information Criteria (AIC/BIC/AICc)

**Add columns to output:**
```sql
→ TABLE (
    ...,
    aic DOUBLE,
    bic DOUBLE,
    aicc DOUBLE
)
```

**Implementation:**
```cpp
// For models that support it
if (auto *arima_model = dynamic_cast<anofoxtime::models::ARIMA*>(model.get())) {
    auto aic = arima_model->aic();
    output.data[6].SetValue(i, aic.has_value() ? Value::DOUBLE(*aic) : Value());
}
```

### 6. ENSEMBLE Table Function

**Signature:**
```sql
ENSEMBLE(
    timestamp_col VARCHAR,
    value_col VARCHAR,
    models VARCHAR[],
    horizon INTEGER,
    method VARCHAR DEFAULT 'Mean',
    ensemble_config STRUCT DEFAULT {}
) → TABLE (
    forecast_step INTEGER,
    point_forecast DOUBLE,
    lower_95 DOUBLE,
    upper_95 DOUBLE,
    weights STRUCT,
    individual_forecasts STRUCT
)
```

**Example:**
```sql
SELECT * FROM ENSEMBLE(
    sales, 'date', 'amount',
    ['Naive', 'SES', 'Theta', 'AutoETS'],
    12,
    'WeightedAccuracy',
    {'accuracy_metric': 'MAE'}
)
GROUP BY product_id;
```

### 7. BACKTEST Table Function

**Signature:**
```sql
BACKTEST(
    timestamp_col VARCHAR,
    value_col VARCHAR,
    model VARCHAR,
    backtest_config STRUCT,
    model_params STRUCT DEFAULT {}
) → TABLE (
    fold INTEGER,
    train_start INTEGER,
    train_end INTEGER,
    test_start INTEGER,
    test_end INTEGER,
    mae DOUBLE,
    rmse DOUBLE,
    mape DOUBLE
)
```

**Example:**
```sql
SELECT 
    product_id,
    AVG(mae) as avg_mae,
    AVG(rmse) as avg_rmse
FROM BACKTEST(
    sales, 'date', 'amount', 'Theta',
    {'min_train': 24, 'horizon': 12, 'step': 6, 'max_folds': 5},
    {'seasonal_period': 12}
)
GROUP BY product_id;
```

## Implementation Priority

### Week 1-2: Complete Model Support
- Add all exponential smoothing models
- Add ARIMA family
- Add Theta variants
- Update tests for each model

### Week 3-4: STRUCT Parameters & Table Input
- Implement named parameter support
- Fix table input handling
- Implement GROUP BY functionality
- Performance optimization for batch forecasting

### Week 5-6: Advanced Features
- ENSEMBLE function
- BACKTEST function  
- Proper prediction intervals
- Information criteria (AIC/BIC)

### Week 7-8: Polish & Documentation
- Scalar utility functions
- Performance benchmarks
- Comprehensive documentation
- Example workflows

## Testing Strategy

### Unit Tests
- Each model individually
- Parameter validation
- Edge cases (empty data, single point, etc.)

### Integration Tests
- GROUP BY batch forecasting
- Ensemble methods
- Backtesting workflows
- Performance benchmarks

### Regression Tests
- Ensure Phase 1 functionality remains stable
- Backward compatibility

## Performance Considerations

### Parallelization
- DuckDB automatically parallelizes GROUP BY
- Each group processed independently
- No manual thread management needed

### Memory Management
- Stream results to avoid loading all forecasts in memory
- Use DataChunk-based output
- Proper cleanup in destructors

### Optimization Opportunities
- Cache model configurations
- Reuse builders where possible
- Vectorize operations where applicable

## Breaking Changes (None Expected)

Phase 2 will be backward compatible with Phase 1:
- Existing queries will continue to work
- New features are additive
- API extensions, not modifications

## Migration Guide (Phase 1 → Phase 2)

No migration needed! Phase 1 code will work as-is.

Optional enhancements available in Phase 2:
```sql
-- Phase 1:
SELECT * FROM FORECAST('date', 'sales', 'SMA', 12, NULL);

-- Phase 2 (enhanced):
SELECT * FROM FORECAST(sales_table, 'date', 'sales', 'SMA', 12, {'window': 7})
GROUP BY product_id;
```

## Success Criteria

Phase 2 is complete when:
- [ ] All 30+ models from anofox-time are available
- [ ] STRUCT parameters work with all types
- [ ] GROUP BY batch forecasting works
- [ ] ENSEMBLE() function implemented
- [ ] BACKTEST() function implemented
- [ ] Prediction intervals are statistically valid
- [ ] All tests pass
- [ ] Documentation is comprehensive
- [ ] Performance benchmarks show competitive speeds

## Questions & Next Steps

Before starting Phase 2:
1. Confirm priority of features
2. Define performance targets
3. Decide on API for ensemble weights
4. Specify backtest output format details
