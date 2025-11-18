# Basic Forecasting Guide

## Introduction

This guide covers the forecasting workflow using the Anofox Forecast extension's SQL API. Topics include data preparation, model selection, forecast generation, and accuracy evaluation.

**API Functions Covered**: `TS_FORECAST()`, `TS_FORECAST_BY()`, evaluation metrics, and data preparation macros.

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

### Forecast Output Schema

| Column | Type | Description |
|--------|------|-------------|
| `forecast_step` | INTEGER | Horizon step (1, 2, 3, ..., horizon) |
| `date_col` | DATE\|TIMESTAMP\|INTEGER | Forecast timestamp (type matches input) |
| `point_forecast` | DOUBLE | Point forecast value |
| `lower` | DOUBLE | Lower prediction interval bound |
| `upper` | DOUBLE | Upper prediction interval bound |
| `model_name` | VARCHAR | Model name used for forecast |
| `insample_fitted` | DOUBLE[] | Fitted values (optional, via `return_insample: true`) |
| `confidence_level` | DOUBLE | Confidence level used for intervals |

**Behavioral Notes**:

- Date column type preserved from input (INTEGER, DATE, or TIMESTAMP)
- Prediction intervals computed at specified confidence level (default 0.90)
- `insample_fitted` array has length equal to training data size (empty by default)

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
