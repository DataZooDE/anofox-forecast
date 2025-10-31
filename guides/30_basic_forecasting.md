# Basic Forecasting Guide

## Introduction

This guide covers the fundamentals of time series forecasting using anofox-forecast. By the end, you'll understand how to prepare data, select models, and evaluate forecasts.

## What is Time Series Forecasting?

Time series forecasting predicts future values based on historical patterns. Common patterns include:
- **Trend**: Long-term increase or decrease
- **Seasonality**: Regular repeating patterns (weekly, monthly, yearly)
- **Cycles**: Non-regular fluctuations
- **Noise**: Random variation

## The Forecasting Workflow

```
Raw Data → Data Preparation → Forecasting → Evaluation → Deployment
```

### 1. Data Preparation

#### Check Data Quality

```sql
-- Generate statistics
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_raw', product_id, date, amount);

-- View summary
SELECT * FROM TS_DATASET_SUMMARY('sales_stats');

-- Quality report
SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30);
```

#### Handle Common Issues

```sql
-- Fill time gaps
CREATE TABLE sales_filled AS
SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, amount);

-- Remove constant series
CREATE TABLE sales_clean AS
SELECT * FROM TS_DROP_CONSTANT('sales_filled', product_id, amount);

-- Fill missing values
CREATE TABLE sales_complete AS
SELECT * FROM TS_FILL_NULLS_FORWARD('sales_clean', product_id, date, amount);
```

### 2. Detect Seasonality

```sql
-- Automatically detect seasonal periods
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales_complete', product_id, date, amount);

-- Result:
-- | product_id | detected_periods | primary_period | is_seasonal |
-- |------------|------------------|----------------|-------------|
-- | P001       | [7, 30]          | 7              | true        |
```

### 3. Generate Forecasts

#### Single Series

```sql
SELECT * FROM TS_FORECAST(
    'sales_complete',
    date,
    amount,
    'AutoETS',  -- Automatic model selection
    28,         -- 28 days ahead
    {'seasonal_period': 7}
);
```

#### Multiple Series

```sql
SELECT * FROM TS_FORECAST_BY(
    'sales_complete',
    product_id,     -- Parallel forecasting per product
    date,
    amount,
    'AutoETS',
    28,
    {'seasonal_period': 7, 'confidence_level': 0.95}
);
```

### 4. Evaluate Forecasts

#### Accuracy Metrics

```sql
-- Assuming you have actual values for the forecast period
WITH actuals AS (
    SELECT product_id, date, actual_sales
    FROM sales_actuals
),
forecasts AS (
    SELECT product_id, date_col AS date, point_forecast
    FROM ts_forecast_result
)
SELECT 
    f.product_id,
    ROUND(TS_MAE(LIST(a.actual_sales), LIST(f.point_forecast)), 2) AS mae,
    ROUND(TS_RMSE(LIST(a.actual_sales), LIST(f.point_forecast)), 2) AS rmse,
    ROUND(TS_MAPE(LIST(a.actual_sales), LIST(f.point_forecast)), 2) AS mape
FROM forecasts f
JOIN actuals a ON f.product_id = a.product_id AND f.date = a.date
GROUP BY f.product_id;
```

#### Interval Coverage

```sql
-- Check if 95% intervals actually cover 95% of actuals
SELECT 
    product_id,
    ROUND(TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) * 100, 1) AS coverage_pct
FROM results
GROUP BY product_id;

-- Target: ~95% for well-calibrated 95% CI
```

## Understanding the Output

### Forecast Columns

| Column | Type | Description |
|--------|------|-------------|
| `forecast_step` | INT | Step ahead (1, 2, 3, ...) |
| `date_col` | TIMESTAMP | Forecast date |
| `point_forecast` | DOUBLE | Predicted value |
| `lower` | DOUBLE | Lower confidence bound |
| `upper` | DOUBLE | Upper confidence bound |
| `model_name` | VARCHAR | Model used |
| `insample_fitted` | DOUBLE[] | Fitted values (if requested) |
| `confidence_level` | DOUBLE | CI level used |

### Interpreting Results

**Point Forecast**: Best estimate (mean/median of distribution)

**Confidence Intervals**:
- 90% CI: 90% chance actual value falls within [lower, upper]
- Wider intervals = more uncertainty
- Intervals grow with forecast horizon

**Example**:
```
forecast_step=1: [95, 105] (width=10)
forecast_step=7: [85, 115] (width=30) ← More uncertain
```

## Model Selection Guide

### For Beginners: Use AutoETS

```sql
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, 
                          {'seasonal_period': 7});
```

**Why**: Automatically selects optimal parameters, works for most cases.

### When to Use Specific Models

| Pattern | Recommended Model | Example |
|---------|-------------------|---------|
| **Trend only** | Naive, Holt | Stock prices |
| **Seasonality only** | SeasonalNaive | Weekly sales (flat trend) |
| **Trend + Seasonality** | ETS, AutoETS, HoltWinters | Monthly revenue |
| **Multiple seasonality** | MSTL, TBATS | Hourly data (daily + weekly) |
| **Intermittent demand** | Croston, ADIDA | Spare parts sales |
| **Complex patterns** | AutoARIMA | Economic indicators |

### Quick Model Comparison

```sql
-- Try 3 models and compare
WITH ets AS (
    SELECT 'AutoETS' AS model, * 
    FROM TS_FORECAST('sales', date, amount, 'AutoETS', 14, {'seasonal_period': 7})
),
theta AS (
    SELECT 'Theta' AS model, * 
    FROM TS_FORECAST('sales', date, amount, 'Theta', 14, {'seasonal_period': 7})
),
naive AS (
    SELECT 'SeasonalNaive' AS model, * 
    FROM TS_FORECAST('sales', date, amount, 'SeasonalNaive', 14, {'seasonal_period': 7})
)
SELECT * FROM ets 
UNION ALL SELECT * FROM theta 
UNION ALL SELECT * FROM naive
ORDER BY model, forecast_step;
```

## Common Parameters

### Essential Parameters

```sql
{
    'seasonal_period': INT,        -- REQUIRED for seasonal models
                                   -- 7=weekly, 30=monthly, 365=yearly
    
    'confidence_level': DOUBLE,    -- Default: 0.90 (90% CI)
                                   -- Common: 0.80, 0.90, 0.95, 0.99
    
    'return_insample': BOOLEAN     -- Default: false
                                   -- true = get fitted values for diagnostics
}
```

### Model-Specific Parameters

#### ETS Models
```sql
{
    'seasonal_period': 7,
    'error_type': 0,      -- 0=additive, 1=multiplicative
    'trend_type': 1,      -- 0=none, 1=additive, 2=damped
    'season_type': 1      -- 0=none, 1=additive, 2=multiplicative
}
```

#### ARIMA Models
```sql
{
    'p': 1,              -- AR order
    'd': 1,              -- Differencing
    'q': 1,              -- MA order
    'seasonal_period': 7
}
```

#### Theta Models
```sql
{
    'seasonal_period': 7,
    'theta': 2.0         -- Theta parameter (default: 2.0)
}
```

## Best Practices

### 1. Always Analyze First

```sql
-- Check data quality before forecasting
CREATE TABLE stats AS SELECT * FROM TS_STATS('sales', product_id, date, amount);

-- Look for problems
SELECT * FROM TS_GET_PROBLEMATIC('stats', 0.7);
```

### 2. Detect Seasonality

```sql
-- Don't guess - detect!
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales', product_id, date, amount);

-- Use detected period in forecast
WITH seasonality AS (
    SELECT product_id, primary_period
    FROM TS_DETECT_SEASONALITY_ALL('sales', product_id, date, amount)
)
SELECT f.*
FROM TS_FORECAST_BY('sales', product_id, date, amount, 'AutoETS', 28,
                    {'seasonal_period': (SELECT primary_period FROM seasonality WHERE product_id = s.product_id)}) f;
```

### 3. Start Simple, Then Optimize

```sql
-- Step 1: Start with AutoETS
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28, {'seasonal_period': 7});

-- Step 2: If needed, try specialized models
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoARIMA', 28, {'seasonal_period': 7});

-- Step 3: Fine-tune parameters
SELECT * FROM TS_FORECAST('sales', date, amount, 'ETS', 28, 
                          {'seasonal_period': 7, 'trend_type': 2, 'season_type': 1});
```

### 4. Always Validate

```sql
-- Get in-sample fitted values
CREATE TABLE forecast_with_fit AS
SELECT * FROM TS_FORECAST('sales', date, amount, 'AutoETS', 28,
                          {'seasonal_period': 7, 'return_insample': true});

-- Check fit quality
WITH residuals AS (
    SELECT 
        s.amount AS actual,
        UNNEST(f.insample_fitted) AS fitted
    FROM sales s, forecast_with_fit f
)
SELECT 
    TS_R2(LIST(actual), LIST(fitted)) AS r_squared,
    TS_RMSE(LIST(actual), LIST(fitted)) AS rmse
FROM residuals;

-- R² > 0.7 indicates good fit
```

## Complete Example: Sales Forecasting

```sql
-- ============================================================================
-- Real-world sales forecasting workflow
-- ============================================================================

-- Step 1: Prepare data
CREATE TABLE sales_prep AS
WITH filled AS (
    SELECT * FROM TS_FILL_GAPS('sales_raw', product_id, date, sales_amount)
),
cleaned AS (
    SELECT * FROM TS_DROP_CONSTANT('filled', product_id, sales_amount)
),
complete AS (
    SELECT * FROM TS_FILL_NULLS_FORWARD('cleaned', product_id, date, sales_amount)
)
SELECT * FROM complete;

-- Step 2: Analyze data
CREATE TABLE sales_stats AS
SELECT * FROM TS_STATS('sales_prep', product_id, date, sales_amount);

SELECT * FROM TS_QUALITY_REPORT('sales_stats', 30);

-- Step 3: Detect seasonality
CREATE TABLE seasonality AS
SELECT * FROM TS_DETECT_SEASONALITY_ALL('sales_prep', product_id, date, sales_amount);

SELECT * FROM seasonality;

-- Step 4: Generate forecasts
CREATE TABLE forecasts AS
SELECT * FROM TS_FORECAST_BY('sales_prep', product_id, date, sales_amount,
                             'AutoETS', 28,
                             {'seasonal_period': 7, 
                              'confidence_level': 0.95,
                              'return_insample': true});

-- Step 5: Review forecasts
SELECT 
    product_id,
    forecast_step,
    ROUND(point_forecast, 2) AS forecast,
    ROUND(lower, 2) AS lower_95,
    ROUND(upper, 2) AS upper_95,
    model_name
FROM forecasts
WHERE forecast_step <= 7
ORDER BY product_id, forecast_step;

-- Step 6: Validate (if you have actuals)
SELECT 
    product_id,
    TS_MAE(LIST(actual), LIST(forecast)) AS mae,
    TS_COVERAGE(LIST(actual), LIST(lower), LIST(upper)) * 100 AS coverage_pct
FROM evaluation
GROUP BY product_id;
```

## Summary

**You've learned**:
- ✅ Complete forecasting workflow
- ✅ Data preparation steps
- ✅ Model selection basics
- ✅ Parameter configuration
- ✅ Result interpretation
- ✅ Forecast validation

**Next**: [Model Selection Guide](40_model_selection.md) - Choose the best model for your data

---

**Questions?** Check the [API Reference](90_api_reference.md) or [FAQ](50_faq.md)!

