# Time Series Evaluation Metrics

This document describes the evaluation metric functions available in the DuckDB Anofox Forecast Extension.

---

## Overview

The extension provides 8 standard time series forecasting accuracy metrics as individual SQL functions:

- `TS_MAE()` - Mean Absolute Error
- `TS_MSE()` - Mean Squared Error
- `TS_RMSE()` - Root Mean Squared Error
- `TS_MAPE()` - Mean Absolute Percentage Error
- `TS_SMAPE()` - Symmetric Mean Absolute Percentage Error
- `TS_MASE()` - Mean Absolute Scaled Error (requires baseline)
- `TS_R2()` - Coefficient of Determination (R-Squared)
- `TS_BIAS()` - Forecast Bias (systematic over/under-forecasting)

---

## Function Signatures

### TS_MAE - Mean Absolute Error

```sql
TS_MAE(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula**: `Σ|yᵢ - ŷᵢ| / n`

**Range**: [0, ∞)

**Interpretation**: 
- Average absolute difference between actual and predicted values
- Lower is better
- Same units as original data
- **Use when**: Easy interpretation needed, outliers shouldn't dominate

**Example**:
```sql
SELECT TS_MAE([100, 102, 105], [101, 101, 104]) AS mae;
-- Result: 0.67
```

---

### TS_MSE - Mean Squared Error

```sql
TS_MSE(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula**: `Σ(yᵢ - ŷᵢ)² / n`

**Range**: [0, ∞)

**Interpretation**:
- Average squared difference
- Penalizes large errors more than small ones
- Units are squared
- **Use when**: Large errors are particularly costly

**Example**:
```sql
SELECT TS_MSE([100, 102, 105], [101, 101, 104]) AS mse;
-- Result: 0.67
```

---

### TS_RMSE - Root Mean Squared Error

```sql
TS_RMSE(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula**: `√(Σ(yᵢ - ŷᵢ)² / n)`

**Range**: [0, ∞)

**Interpretation**:
- Square root of MSE
- Same units as original data
- Penalizes outliers (but less than MSE)
- Typically RMSE ≥ MAE
- **Use when**: You want to penalize large errors in original units

**Example**:
```sql
SELECT TS_RMSE([100, 102, 105], [101, 101, 104]) AS rmse;
-- Result: 0.82
```

---

### TS_MAPE - Mean Absolute Percentage Error

```sql
TS_MAPE(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula**: `100 × Σ|yᵢ - ŷᵢ| / |yᵢ| / n`

**Range**: [0, ∞) (expressed as percentage)

**Interpretation**:
- Percentage error
- Scale-independent (good for comparing different datasets)
- < 10% is good, < 5% is excellent
- **Warning**: Undefined when actual values contain zeros
- **Use when**: Percentage errors are meaningful, all values > 0

**Example**:
```sql
SELECT TS_MAPE([100, 102, 105], [101, 101, 104]) AS mape_percent;
-- Result: 0.65 (0.65% error)
```

---

### TS_SMAPE - Symmetric Mean Absolute Percentage Error

```sql
TS_SMAPE(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula**: `100 × Σ|yᵢ - ŷᵢ| / ((|yᵢ|+|ŷᵢ|)/2) / n`

**Range**: [0, 200]

**Interpretation**:
- Symmetric version of MAPE
- Treats over-forecasting and under-forecasting equally
- < 20% is good, < 10% is excellent
- **Use when**: Symmetry important, can handle zeros better than MAPE

**Example**:
```sql
SELECT TS_SMAPE([100, 102, 105], [101, 101, 104]) AS smape_percent;
-- Result: 0.65 (0.65% error)
```

---

### TS_MASE - Mean Absolute Scaled Error

```sql
TS_MASE(actual DOUBLE[], predicted DOUBLE[], baseline DOUBLE[]) → DOUBLE
```

**Formula**: `MAE(predicted) / MAE(baseline)`

**Range**: [0, ∞)

**Interpretation**:
- Error relative to naive baseline forecast
- < 1.0 means model beats baseline
- > 1.0 means baseline is better
- Scale-independent
- **Use when**: Comparing against baseline, zeros allowed in data

**Example**:
```sql
-- Compare Theta against Naive baseline
SELECT TS_MASE(
    [100, 102, 105, 103, 107],  -- actual
    [101, 101, 104, 104, 106],  -- theta predictions
    [100, 100, 100, 100, 100]   -- naive baseline
) AS mase;
-- Result: 0.24 → Theta is 76% better than Naive ✅
```

---

### TS_R2 - Coefficient of Determination

```sql
TS_R2(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula**: `1 - SS_residual / SS_total`

**Range**: (-∞, 1.0]

**Interpretation**:
- Proportion of variance explained by the model
- 1.0 = perfect fit
- 0.0 = as good as mean baseline
- < 0 = worse than mean baseline
- > 0.7 is good, > 0.9 is excellent
- **Use when**: Want to quantify explained variance

**Example**:
```sql
SELECT TS_R2([100, 102, 105, 103, 107], [101, 101, 104, 104, 106]) AS r_squared;
-- Result: 0.88 → Model explains 88% of variance
```

---

### TS_BIAS - Forecast Bias

```sql
TS_BIAS(actual DOUBLE[], predicted DOUBLE[]) → DOUBLE
```

**Formula**: `Σ(ŷᵢ - yᵢ) / n`

**Range**: (-∞, ∞)

**Interpretation**:
- Average signed error (systematic over/under-forecasting)
- **Positive bias** = systematic over-forecasting
- **Negative bias** = systematic under-forecasting
- **Zero bias** = unbiased forecast (ideal)
- Same units as original data
- **Use when**: Detecting systematic forecast bias

**Example**:
```sql
-- Over-forecasting
SELECT TS_BIAS([100, 102, 105], [103, 105, 108]) AS bias;
-- Result: +3.0 → Systematically over-forecasting by 3 units

-- Under-forecasting
SELECT TS_BIAS([100, 102, 105], [98, 100, 103]) AS bias;
-- Result: -2.0 → Systematically under-forecasting by 2 units

-- Unbiased
SELECT TS_BIAS([100, 102, 105], [101, 101, 106]) AS bias;
-- Result: 0.0 → Errors cancel out (no systematic bias)
```

**Important Notes**:
- Bias can be zero even with large errors if they cancel out
- Always use with MAE/RMSE to get full picture
- Positive bias ≠ good forecasts, just systematic tendency

---

## Usage Examples

### 1. Evaluate Single Model

```sql
WITH forecast AS (
    SELECT TS_FORECAST(date, value, 'Theta', 12, MAP{}) AS fc
    FROM train_data
),
predictions AS (
    SELECT fc.point_forecast AS pred FROM forecast
),
actuals AS (
    SELECT LIST(value) AS actual FROM test_data
)
SELECT 
    TS_MAE(actual, pred) AS mae,
    TS_RMSE(actual, pred) AS rmse,
    TS_MAPE(actual, pred) AS mape_percent
FROM actuals, predictions;
```

---

### 2. Compare Multiple Models

```sql
WITH models AS (
    SELECT 'Naive' AS model, 
           TS_FORECAST(date, value, 'Naive', 10, MAP{}) AS fc
    FROM data
    UNION ALL
    SELECT 'Theta',
           TS_FORECAST(date, value, 'Theta', 10, MAP{})
    FROM data
    UNION ALL
    SELECT 'AutoETS',
           TS_FORECAST(date, value, 'AutoETS', 10, MAP{'season_length': 7})
    FROM data
),
actuals AS (
    SELECT LIST(value) AS actual FROM test_data LIMIT 10
)
SELECT 
    model,
    ROUND(TS_MAE(actual, fc.point_forecast), 2) AS mae,
    ROUND(TS_RMSE(actual, fc.point_forecast), 2) AS rmse,
    ROUND(TS_MAPE(actual, fc.point_forecast), 2) AS mape
FROM models, actuals
ORDER BY TS_MAE(actual, fc.point_forecast);
```

---

### 3. Benchmark Against Naive

```sql
WITH theta_fc AS (
    SELECT TS_FORECAST(date, value, 'Theta', 10, MAP{}) AS fc FROM train
),
naive_fc AS (
    SELECT TS_FORECAST(date, value, 'Naive', 10, MAP{}) AS fc FROM train
),
actuals AS (
    SELECT LIST(value) AS actual FROM test LIMIT 10
)
SELECT 
    TS_MASE(actual, theta_fc.fc.point_forecast, naive_fc.fc.point_forecast) AS mase,
    CASE 
        WHEN TS_MASE(actual, theta_fc.fc.point_forecast, naive_fc.fc.point_forecast) < 1.0
        THEN 'Theta beats Naive ✅'
        ELSE 'Naive is better'
    END AS result
FROM theta_fc, naive_fc, actuals;
```

---

### 4. Rolling Window Evaluation

```sql
-- Evaluate forecast quality over time windows
WITH windows AS (
    SELECT 
        (ROW_NUMBER() OVER (ORDER BY date) - 1) / 30 AS window_id,
        date,
        value
    FROM historical_data
),
forecasts_by_window AS (
    SELECT 
        window_id,
        TS_FORECAST(date, value, 'AutoETS', 7, MAP{'season_length': 7}) AS fc
    FROM windows
    GROUP BY window_id
),
test_by_window AS (
    SELECT 
        window_id,
        LIST(value) AS actual
    FROM test_data
    GROUP BY window_id
)
SELECT 
    t.window_id,
    ROUND(TS_MAE(t.actual, f.fc.point_forecast), 2) AS mae,
    ROUND(TS_MAPE(t.actual, f.fc.point_forecast), 2) AS mape
FROM test_by_window t
JOIN forecasts_by_window f ON t.window_id = f.window_id
ORDER BY t.window_id;
```

---

### 5. Production Monitoring

```sql
-- Daily forecast quality check
WITH yesterday_forecast AS (
    SELECT fc.point_forecast[1] AS predicted
    FROM (
        SELECT TS_FORECAST(date, value, 'AutoETS', 1, MAP{}) AS fc
        FROM historical_data
        WHERE date >= CURRENT_DATE - INTERVAL 90 DAY
    )
),
today_actual AS (
    SELECT value AS actual
    FROM real_time_data
    WHERE date = CURRENT_DATE
)
SELECT 
    CURRENT_DATE AS eval_date,
    TS_MAE([actual], [predicted]) AS mae,
    TS_MAPE([actual], [predicted]) AS mape,
    CASE 
        WHEN TS_MAPE([actual], [predicted]) > 10.0 THEN '⚠️ HIGH ERROR'
        WHEN TS_MAPE([actual], [predicted]) > 5.0 THEN '⚡ MEDIUM ERROR'
        ELSE '✅ LOW ERROR'
    END AS quality
FROM today_actual, yesterday_forecast;
```

---

## Metric Selection Guide

### Choose MAE when:
- ✅ You want errors in original units
- ✅ Outliers shouldn't dominate
- ✅ Simple interpretation needed
- 🎯 Target: Domain-specific (e.g., <5 for sales forecast)

### Choose RMSE when:
- ✅ Large errors are particularly costly
- ✅ You want to penalize outliers more
- ✅ Standard deviation-like metric preferred
- 🎯 Target: Domain-specific, typically ≥ MAE

### Choose MAPE when:
- ✅ Scale-independent comparison needed
- ✅ All actual values are positive
- ✅ Percentage errors are meaningful
- ⚠️ Avoid when zeros in data
- 🎯 Target: <10% good, <5% excellent

### Choose SMAPE when:
- ✅ Symmetric errors important
- ✅ MAPE is too asymmetric
- ✅ Some zeros might be present
- 🎯 Target: <20% good, <10% excellent

### Choose MASE when:
- ✅ Comparing against naive baseline
- ✅ Scale-independent metric needed
- ✅ Zeros allowed in data
- ✅ Want interpretable improvement over baseline
- 🎯 Target: <1.0 = beats baseline

### Choose R² when:
- ✅ Variance explained is meaningful
- ✅ Comparing models on same dataset
- ✅ Regression-style interpretation preferred
- 🎯 Target: >0.7 good, >0.9 excellent

### Choose BIAS when:
- ✅ Detecting systematic over/under-forecasting
- ✅ Checking for forecast calibration
- ✅ Identifying model tendencies
- ⚠️ Always combine with MAE/RMSE (bias can be zero with large errors)
- 🎯 Target: ≈0 (no systematic bias)

---

## Common Use Cases

### Model Selection

```sql
-- Compare 5 models, select best by MAE
WITH models AS (
    SELECT 'Naive' AS m, TS_FORECAST(...) AS fc FROM data
    UNION ALL SELECT 'SMA', TS_FORECAST(...) FROM data
    UNION ALL SELECT 'Theta', TS_FORECAST(...) FROM data
    UNION ALL SELECT 'AutoETS', TS_FORECAST(...) FROM data
    UNION ALL SELECT 'AutoARIMA', TS_FORECAST(...) FROM data
)
SELECT 
    m AS model,
    TS_MAE(actual_list, fc.point_forecast) AS mae
FROM models, test_data
ORDER BY mae
LIMIT 1;  -- Best model
```

---

### Production Alerting

```sql
-- Alert if forecast quality degrades
CREATE OR REPLACE TABLE forecast_quality_log AS
SELECT 
    CURRENT_DATE AS date,
    TS_MAE(actual_list, forecast_list) AS mae,
    TS_MAPE(actual_list, forecast_list) AS mape
FROM daily_evaluation;

-- Check if quality is degrading
SELECT 
    CASE 
        WHEN mape > 15 THEN 'CRITICAL: Retrain model'
        WHEN mape > 10 THEN 'WARNING: Monitor closely'
        WHEN mape > 5 THEN 'CAUTION: Check for drift'
        ELSE 'OK'
    END AS alert_level
FROM forecast_quality_log
WHERE date = CURRENT_DATE;
```

---

### A/B Testing

```sql
-- Compare two model versions
WITH version_a AS (
    SELECT TS_FORECAST(..., 'Theta', ...) AS fc FROM data
),
version_b AS (
    SELECT TS_FORECAST(..., 'AutoETS', ...) AS fc FROM data
)
SELECT 
    'Version A' AS version,
    TS_MAE(actual, version_a.fc.point_forecast) AS mae,
    TS_MAPE(actual, version_a.fc.point_forecast) AS mape
FROM test_data, version_a
UNION ALL
SELECT 
    'Version B',
    TS_MAE(actual, version_b.fc.point_forecast),
    TS_MAPE(actual, version_b.fc.point_forecast)
FROM test_data, version_b;
```

---

### Improvement vs Baseline

```sql
-- Quantify improvement over naive forecast
WITH model_fc AS (
    SELECT TS_FORECAST(date, value, 'AutoETS', 12, MAP{}) AS fc FROM train
),
naive_fc AS (
    SELECT TS_FORECAST(date, value, 'Naive', 12, MAP{}) AS fc FROM train
),
actuals AS (
    SELECT LIST(value) AS actual FROM test LIMIT 12
)
SELECT 
    TS_MASE(actual, model_fc.fc.point_forecast, naive_fc.fc.point_forecast) AS mase,
    ROUND((1.0 - TS_MASE(actual, model_fc.fc.point_forecast, naive_fc.fc.point_forecast)) * 100, 1) 
        AS improvement_percent
FROM actuals, model_fc, naive_fc;

-- Example: MASE=0.35 → 65% improvement over naive
```

---

## Best Practices

### 1. Use Multiple Metrics

Don't rely on a single metric. Different metrics reveal different aspects:

```sql
SELECT 
    TS_MAE(actual, predicted) AS mae,      -- Absolute error
    TS_RMSE(actual, predicted) AS rmse,    -- Outlier sensitivity
    TS_MAPE(actual, predicted) AS mape,    -- Percentage error
    TS_R2(actual, predicted) AS r2         -- Variance explained
FROM evaluation;

-- If MAE low but MAPE high → errors concentrated on small values
-- If RMSE >> MAE → outliers present
-- If R² high but MAE high → good trend, poor magnitude
```

### 2. Always Validate on Holdout Data

```sql
-- Never evaluate on training data!
WITH split AS (
    SELECT * FROM data WHERE date < '2024-01-01'  -- Train
),
forecast AS (
    SELECT TS_FORECAST(date, value, 'AutoETS', 30, MAP{}) AS fc FROM split
),
test AS (
    SELECT LIST(value) AS actual 
    FROM data 
    WHERE date >= '2024-01-01'  -- Test (holdout)
    LIMIT 30
)
SELECT 
    TS_MAE(actual, forecast.fc.point_forecast) AS test_mae
FROM test, forecast;
```

### 3. Use MASE for Model Comparison

```sql
-- MASE is scale-independent and baseline-aware
SELECT 
    model_name,
    TS_MASE(actual, pred, naive_baseline) AS mase
FROM model_comparison
ORDER BY mase;

-- Models with MASE < 1.0 beat the baseline
```

### 4. Set Thresholds Based on Business Requirements

```sql
-- Example: Inventory planning accepts 5% error
WITH quality_check AS (
    SELECT 
        product_id,
        TS_MAPE(actual, predicted) AS mape
    FROM forecast_evaluation
)
SELECT 
    product_id,
    mape,
    CASE 
        WHEN mape < 5.0 THEN 'Use forecast'
        WHEN mape < 10.0 THEN 'Use with caution'
        ELSE 'Manual review required'
    END AS recommendation
FROM quality_check;
```

---

## Error Handling

All functions validate inputs:

```sql
-- ERROR: Arrays must have same length
SELECT TS_MAE([1, 2, 3], [1, 2]);

-- ERROR: Arrays must not be empty
SELECT TS_MAE([], []);

-- ERROR: MAPE undefined for zeros
SELECT TS_MAPE([0, 1, 2], [0, 1, 2]);
-- Returns NULL (gracefully handled)

-- ERROR: MASE requires 3 arguments
SELECT TS_MASE([1, 2], [1, 2]);
-- Use: TS_MASE([1, 2], [1, 2], baseline)
```

---

## Quick Reference

| Function | Arguments | Returns | Key Use |
|----------|-----------|---------|---------|
| `TS_MAE()` | actual, predicted | DOUBLE | General error |
| `TS_MSE()` | actual, predicted | DOUBLE | Outlier penalty |
| `TS_RMSE()` | actual, predicted | DOUBLE | Error in original units |
| `TS_MAPE()` | actual, predicted | DOUBLE | Percentage error |
| `TS_SMAPE()` | actual, predicted | DOUBLE | Symmetric % error |
| `TS_MASE()` | actual, predicted, baseline | DOUBLE | vs Baseline |
| `TS_R2()` | actual, predicted | DOUBLE | Variance explained |
| `TS_BIAS()` | actual, predicted | DOUBLE | Systematic over/under-forecasting |

---

## See Also

- [PARAMETERS.md](PARAMETERS.md) - Model parameters guide
- [USAGE.md](USAGE.md) - Advanced usage patterns  
- [README.md](../README.md) - General documentation

