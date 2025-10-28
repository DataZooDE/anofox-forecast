# Understanding Forecasts - Statistical Guide

## Introduction

This guide explains the statistical concepts behind time series forecasting, helping you understand what forecasts mean and how to interpret them correctly.

## Time Series Components

Every time series can be decomposed into:

### 1. Trend (T)
Long-term increase or decrease in the data.

**Example**: Revenue growing 5% year-over-year

```sql
-- Detect trend using correlation
WITH stats AS (
    SELECT * FROM TS_STATS('sales', product_id, date, amount)
)
SELECT 
    series_id,
    trend_corr,
    CASE 
        WHEN trend_corr > 0.3 THEN '📈 Strong upward trend'
        WHEN trend_corr < -0.3 THEN '📉 Strong downward trend'
        ELSE '↔️ No clear trend'
    END AS trend_direction
FROM stats;
```

### 2. Seasonality (S)
Regular repeating patterns at fixed intervals.

**Common Periods**:
- Weekly: period = 7
- Monthly: period = 30
- Quarterly: period = 90
- Yearly: period = 365

```sql
-- Detect seasonality automatically
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales', product_id, date, amount);

-- Analyze seasonal strength
SELECT 
    product_id,
    detected_periods,
    primary_period,
    is_seasonal
FROM TS_DETECT_SEASONALITY_ALL('sales', product_id, date, amount)
WHERE is_seasonal = true;
```

### 3. Remainder (R)
Random noise that cannot be explained by trend or seasonality.

**Formula**: `Y_t = T_t + S_t + R_t` (additive model)

## Point Forecasts

### What is a Point Forecast?

The **most likely value** for a future time point, typically the mean or median of the forecast distribution.

```sql
SELECT 
    forecast_step,
    point_forecast    -- This is the expected value
FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7});
```

### Point Forecast Accuracy

**Common Metrics**:

| Metric | Formula | Interpretation | Good Value |
|--------|---------|----------------|------------|
| **MAE** | Mean(|Actual - Forecast|) | Average error | Lower is better |
| **RMSE** | √Mean((Actual - Forecast)²) | Penalizes large errors | Lower is better |
| **MAPE** | Mean(|Actual - Forecast|/Actual) × 100 | % error | < 20% is good |
| **SMAPE** | Symmetric MAPE | Handles zeros better | < 20% is good |

```sql
SELECT 
    TS_MAE(LIST(actual), LIST(forecast)) AS mae,
    TS_RMSE(LIST(actual), LIST(forecast)) AS rmse,
    TS_MAPE(LIST(actual), LIST(forecast)) AS mape,
    TS_SMAPE(LIST(actual), LIST(forecast)) AS smape
FROM results;
```

## Confidence Intervals

### What are Confidence Intervals?

A **range of plausible values** for the forecast, quantifying uncertainty.

**90% CI** = We're 90% confident the actual value will be in this range

```sql
SELECT 
    forecast_step,
    point_forecast,
    lower,              -- Lower bound (5th percentile for 90% CI)
    upper,              -- Upper bound (95th percentile for 90% CI)
    confidence_level    -- Shows CI level used (0.90)
FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28,
                 {'seasonal_period': 7, 'confidence_level': 0.90});
```

### How to Read Confidence Intervals

```
Day 1: Forecast = 100, CI = [95, 105]
→ 90% sure actual will be between 95 and 105

Day 7: Forecast = 100, CI = [85, 115]
→ 90% sure actual will be between 85 and 115
→ Note: Wider interval (more uncertainty)
```

### Interval Width Interpretation

```sql
-- Analyze uncertainty over horizon
SELECT 
    forecast_step,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(upper - lower, 2) AS interval_width,
    ROUND(100 * (upper - lower) / point_forecast, 1) AS width_pct
FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7})
ORDER BY forecast_step;

-- Pattern: Interval width grows with horizon
```

### Choosing Confidence Level

| Level | Use Case | Trade-off |
|-------|----------|-----------|
| **80%** | Aggressive planning | Narrow intervals, more misses |
| **90%** | **Recommended default** | Balanced |
| **95%** | Conservative planning | Wider intervals, fewer misses |
| **99%** | Risk-averse | Very wide, rarely wrong |

```sql
-- Compare confidence levels
SELECT 
    '90% CI' AS level,
    AVG(upper - lower) AS avg_width
FROM TS_FORECAST(..., {'confidence_level': 0.90, ...})
UNION ALL
SELECT 
    '95% CI',
    AVG(upper - lower)
FROM TS_FORECAST(..., {'confidence_level': 0.95, ...})
UNION ALL
SELECT 
    '99% CI',
    AVG(upper - lower)
FROM TS_FORECAST(..., {'confidence_level': 0.99, ...});
```

## Coverage Analysis

### What is Coverage?

**Coverage** = Fraction of actual values that fall within the predicted intervals

```sql
SELECT 
    TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) * 100 AS coverage_pct
FROM results;

-- For 95% CI, expect ~95% coverage
```

### Interpreting Coverage

| Coverage | Status | Meaning |
|----------|--------|---------|
| 95% | ✅ Excellent | Intervals well-calibrated |
| 90% | ⚠️ Acceptable | Slightly optimistic |
| 80% | ❌ Poor | Intervals too narrow |
| 98% | ⚠️ Conservative | Intervals may be too wide |

```sql
-- Check calibration per product
SELECT 
    product_id,
    ROUND(TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) * 100, 1) AS coverage_pct,
    confidence_level * 100 AS expected_coverage_pct,
    CASE 
        WHEN ABS(TS_COVERAGE(...) - confidence_level) < 0.05 
        THEN '✅ Well calibrated'
        WHEN TS_COVERAGE(...) < confidence_level - 0.10
        THEN '❌ Under-coverage (intervals too narrow)'
        ELSE '⚠️ Over-coverage (intervals too wide)'
    END AS calibration
FROM results
GROUP BY product_id, confidence_level;
```

## Residuals & Model Diagnostics

### What are Residuals?

**Residual** = Actual - Fitted Value

Good model → Residuals should be:
- Centered around 0 (no bias)
- Randomly distributed (no patterns)
- Constant variance (homoscedastic)

```sql
-- Get fitted values
CREATE TABLE with_fitted AS
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28,
                          {'seasonal_period': 7, 'return_insample': true});

-- Compute residuals
WITH actuals_fitted AS (
    SELECT 
        s.amount AS actual,
        UNNEST(f.insample_fitted) AS fitted,
        ROW_NUMBER() OVER (ORDER BY s.date) AS idx
    FROM sales s, with_fitted f
)
SELECT 
    idx,
    actual,
    ROUND(fitted, 2) AS fitted,
    ROUND(actual - fitted, 2) AS residual,
    ROUND((actual - fitted) / NULLIF(STDDEV(actual - fitted) OVER (), 0), 2) AS standardized_residual
FROM actuals_fitted;
```

### Residual Diagnostics

```sql
-- Check residual properties
WITH residuals AS (
    SELECT actual - fitted AS resid
    FROM actuals_fitted
)
SELECT 
    ROUND(AVG(resid), 4) AS mean_residual,  -- Should be ~0
    ROUND(STDDEV(resid), 2) AS std_residual,
    ROUND(MIN(resid), 2) AS min_residual,
    ROUND(MAX(resid), 2) AS max_residual,
    COUNT(CASE WHEN ABS(resid) > 2 * STDDEV(resid) OVER () THEN 1 END) AS n_outliers
FROM residuals;

-- Good model: mean_residual ≈ 0, n_outliers < 5%
```

## Forecast Horizon

### How Far Can You Forecast?

**Rule of Thumb**: Reliable up to 1-2 seasonal cycles

| Data Frequency | Seasonal Period | Max Reliable Horizon |
|----------------|-----------------|---------------------|
| Daily | 7 (weekly) | 14-30 days |
| Daily | 365 (yearly) | 2-3 months |
| Weekly | 52 (yearly) | 12-26 weeks |
| Monthly | 12 (yearly) | 12-24 months |

### Uncertainty Growth

```sql
-- Show how uncertainty grows with horizon
SELECT 
    forecast_step,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(upper - lower, 2) AS interval_width,
    ROUND(100 * (upper - lower) / point_forecast, 1) AS relative_uncertainty_pct
FROM TS_FORECAST('sales', date, amount, 'AutoETS', 60, {'seasonal_period': 7})
WHERE forecast_step IN (1, 7, 14, 30, 60);

-- Pattern: relative_uncertainty_pct grows with horizon
```

## Statistical Significance

### Is the Forecast Better than Baseline?

```sql
-- Compare AutoETS vs Naive (baseline)
WITH ets_forecast AS (
    SELECT 'AutoETS' AS model, * 
    FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7})
),
naive_forecast AS (
    SELECT 'Naive' AS model, *
    FROM TS_FORECAST('sales', date, amount, 'Naive', 28, MAP{})
),
evaluation AS (
    SELECT 
        model,
        TS_MAE(LIST(actual), LIST(forecast)) AS mae
    FROM results
    GROUP BY model
)
SELECT 
    model,
    ROUND(mae, 2) AS mae,
    ROUND(100 * (1 - mae / MAX(mae) OVER ()), 1) AS improvement_pct
FROM evaluation
ORDER BY mae;
```

### Relative Metrics

```sql
-- RMAE: Compare two models
SELECT 
    TS_RMAE(
        LIST(actual),
        LIST(autoets_forecast),
        LIST(naive_forecast)
    ) AS relative_mae;

-- < 1.0: AutoETS is better
-- = 1.0: Same performance
-- > 1.0: Naive is better
```

## Model Fit Quality

### R-squared (R²)

**Interpretation**: Proportion of variance explained by the model

```sql
-- Compute R² on training data
WITH fitted AS (
    SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 7,
                              {'return_insample': true, 'seasonal_period': 7})
),
fit_quality AS (
    SELECT 
        TS_R2(LIST(s.amount), f.insample_fitted) AS r_squared
    FROM sales s, fitted f
)
SELECT 
    ROUND(r_squared, 4) AS r_squared,
    CASE 
        WHEN r_squared > 0.9 THEN '🌟 Excellent fit'
        WHEN r_squared > 0.7 THEN '✅ Good fit'
        WHEN r_squared > 0.5 THEN '⚠️ Moderate fit'
        ELSE '❌ Poor fit - try different model'
    END AS assessment
FROM fit_quality;
```

### MASE (Mean Absolute Scaled Error)

**Purpose**: Compare forecast to naive baseline

```sql
SELECT 
    TS_MASE(
        LIST(actual),
        LIST(forecast),
        LIST(naive_forecast)  -- Baseline
    ) AS mase;

-- < 1.0: Better than baseline ✅
-- = 1.0: Same as baseline
-- > 1.0: Worse than baseline ❌
```

## Bias Detection

### Systematic Errors

**Bias** = Average(Forecast - Actual)

- Bias > 0: Systematic over-forecasting
- Bias < 0: Systematic under-forecasting
- Bias ≈ 0: Unbiased (good!)

```sql
SELECT 
    product_id,
    ROUND(TS_BIAS(LIST(actual), LIST(forecast)), 2) AS bias,
    CASE 
        WHEN TS_BIAS(...) > 5 THEN '⚠️ Over-forecasting'
        WHEN TS_BIAS(...) < -5 THEN '⚠️ Under-forecasting'
        ELSE '✅ Unbiased'
    END AS assessment
FROM results
GROUP BY product_id;
```

## Stationarity

### What is Stationarity?

A stationary series has:
- Constant mean over time
- Constant variance
- No seasonality or trend

**Why it matters**: Many models assume (or require) stationarity.

### Making Data Stationary

```sql
-- Remove trend via differencing
CREATE TABLE sales_diff AS
WITH series AS (
    SELECT 
        product_id,
        date,
        amount,
        amount - LAG(amount) OVER (PARTITION BY product_id ORDER BY date) AS diff_amount
    FROM sales
)
SELECT * FROM series WHERE diff_amount IS NOT NULL;

-- Check if stationarity improved
SELECT 
    'Original' AS data,
    STDDEV(amount) / AVG(amount) AS cv
FROM sales
UNION ALL
SELECT 
    'Differenced',
    STDDEV(diff_amount) / AVG(ABS(diff_amount))
FROM sales_diff;

-- Lower CV = more stationary
```

## Autocorrelation

### What is Autocorrelation?

Correlation of a series with itself at different lags.

**Strong autocorrelation** → Predictable patterns → Better forecasts

**Use Case**: Seasonality detection

```sql
-- Use seasonality analyzer for ACF
SELECT * FROM TS_ANALYZE_SEASONALITY(
    LIST(date ORDER BY date),
    LIST(amount ORDER BY date)
)
FROM sales;

-- Returns: trend_strength, seasonal_strength, etc.
```

## Model Types

### Exponential Smoothing (ETS)

**Idea**: Recent observations weighted more heavily

**Components**:
- **E**: Error (additive/multiplicative)
- **T**: Trend (none/additive/damped)
- **S**: Seasonality (none/additive/multiplicative)

```sql
-- Let AutoETS select best configuration
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7});

-- Or specify manually
SELECT * FROM TS_FORECAST('sales', date, amount, 'ETS', 28, {
    'seasonal_period': 7,
    'error_type': 0,      -- 0=additive
    'trend_type': 1,      -- 1=additive
    'season_type': 1      -- 1=additive
});
```

### ARIMA

**Idea**: Autoregressive Integrated Moving Average

**Parameters**:
- **p**: Autoregressive order (past values)
- **d**: Differencing degree (make stationary)
- **q**: Moving average order (past errors)
- **P, D, Q, s**: Seasonal components

```sql
-- Let AutoARIMA select parameters
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoARIMA', 28, {'seasonal_period': 7});

-- Or specify manually
SELECT * FROM TS_FORECAST('sales', date, amount, 'ARIMA', 28, {
    'p': 1, 'd': 1, 'q': 1,
    'P': 1, 'D': 1, 'Q': 1, 's': 7
});
```

### Theta Method

**Idea**: Decompose into trend and seasonal components with θ parameter

**Best for**: Data with trend and seasonality

```sql
SELECT * FROM TS_FORECAST('sales', date, amount, 'Theta', 28, {'seasonal_period': 7});
```

## Forecast Evaluation

### In-Sample vs Out-of-Sample

**In-Sample**: Performance on training data  
**Out-of-Sample**: Performance on unseen test data

```sql
-- In-sample fit
WITH fitted AS (
    SELECT * FROM TS_FORECAST('sales_train', date, amount, 'AutoETS', 1,
                              {'return_insample': true, 'seasonal_period': 7})
)
SELECT 
    'In-sample R²' AS metric,
    ROUND(TS_R2(LIST(actual), insample_fitted), 4) AS value
FROM actuals, fitted;

-- Out-of-sample accuracy
SELECT 
    'Out-of-sample MAPE' AS metric,
    ROUND(TS_MAPE(LIST(test_actual), LIST(forecast)), 2) AS value
FROM test_results;

-- Good model: Out-of-sample ≈ In-sample (no overfitting)
```

### Rolling Window Validation

```sql
-- Cross-validation with expanding window
WITH cv_windows AS (
    SELECT w AS window_id
    FROM generate_series(30, 330, 30) t(w)  -- Every 30 days
),
cv_forecasts AS (
    SELECT 
        window_id,
        fc.*
    FROM cv_windows w
    CROSS JOIN LATERAL (
        SELECT * FROM TS_FORECAST(
            (SELECT * FROM sales WHERE EPOCH(date) / 86400 <= window_id),
            date, amount, 'AutoETS', 30, {'seasonal_period': 7}
        )
    ) fc
)
SELECT 
    window_id AS train_days,
    ROUND(AVG(point_forecast), 2) AS avg_forecast,
    STDDEV(point_forecast) AS forecast_volatility
FROM cv_forecasts
GROUP BY window_id
ORDER BY window_id;
```

## Understanding Model Output

### Fitted Values (In-Sample Forecasts)

**Purpose**: One-step-ahead predictions on training data

```sql
-- Enable fitted values
SELECT 
    LEN(insample_fitted) AS n_observations
FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28,
                 {'return_insample': true, 'seasonal_period': 7});

-- Use for diagnostics
WITH fitted AS (
    SELECT * FROM TS_FORECAST(..., {'return_insample': true, ...})
)
SELECT 
    TS_R2(LIST(actual), insample_fitted) AS training_r2,
    TS_MAE(LIST(actual), insample_fitted) AS training_mae
FROM actuals, fitted;
```

## Statistical Tests

### Residual Autocorrelation Test

```sql
-- Check if residuals are random (good) or patterned (bad)
WITH residuals AS (
    SELECT 
        actual - fitted AS resid,
        LAG(actual - fitted) OVER (ORDER BY date) AS lag1_resid
    FROM actuals_fitted
)
SELECT 
    ROUND(CORR(resid, lag1_resid), 4) AS lag1_autocorrelation,
    CASE 
        WHEN ABS(CORR(resid, lag1_resid)) < 0.2 THEN '✅ Random (good)'
        ELSE '⚠️ Pattern detected (model may be missing something)'
    END AS assessment
FROM residuals;
```

### Heteroscedasticity Test

```sql
-- Check if variance changes over time
WITH residuals AS (
    SELECT 
        NTILE(4) OVER (ORDER BY date) AS quartile,
        actual - fitted AS resid
    FROM actuals_fitted
)
SELECT 
    quartile,
    ROUND(VARIANCE(resid), 2) AS variance,
    COUNT(*) AS n_obs
FROM residuals
GROUP BY quartile;

-- Variance should be similar across quartiles
```

## Advanced Topics

### Quantile Forecasts

```sql
-- Multi-quantile forecasts (not yet implemented)
-- Future feature: Predict distribution, not just mean

-- Workaround: Use different confidence levels
WITH q10 AS (SELECT * FROM TS_FORECAST(..., {'confidence_level': 0.20, ...})),
q50 AS (SELECT * FROM TS_FORECAST(..., {'confidence_level': 1.00, ...})),  -- Point forecast
q90 AS (SELECT * FROM TS_FORECAST(..., {'confidence_level': 0.80, ...}))
SELECT 
    q10.lower AS p10,
    q50.point_forecast AS p50,
    q90.upper AS p90
FROM q10, q50, q90;
```

### Prediction Intervals vs Confidence Intervals

**Prediction Interval**: Range for a future observation  
**Confidence Interval**: Range for the expected value (mean)

In this extension:
- `lower` and `upper` are **prediction intervals**
- Wider than confidence intervals
- What you need for planning!

## Summary

**Key Concepts**:
- ✅ Point forecasts = expected values
- ✅ Confidence intervals = uncertainty quantification
- ✅ Coverage = interval calibration check
- ✅ Residuals = model diagnostic tool
- ✅ In-sample vs out-of-sample = overfitting check
- ✅ Bias = systematic error detection

**Best Practices**:
1. Always validate with out-of-sample data
2. Check coverage matches confidence level
3. Analyze residuals for patterns
4. Use rolling window validation
5. Compare multiple models
6. Monitor forecast accuracy over time

**Next**: [Accuracy Metrics Guide](21_accuracy_metrics.md) - Deep dive into evaluation metrics

---

**Related**:
- [Model Selection](11_model_selection.md) - Choose the right model
- [Confidence Intervals](22_confidence_intervals.md) - More on uncertainty
- [Parameters Guide](12_parameters.md) - Model configuration

