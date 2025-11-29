# Basic Forecasting Guide

## Introduction

This guide covers the forecasting workflow using the Anofox Forecast extension's SQL API. Topics include data preparation, model selection, forecast generation, and accuracy evaluation.

**API Functions Covered**: `anofox_fcst_ts_forecast()`, `anofox_fcst_ts_forecast_by()`, evaluation metrics, and data preparation macros.

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
-- Create sample raw sales data
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Generate statistics
CREATE TABLE sales_stats AS
SELECT * FROM anofox_fcst_ts_stats('sales_raw', product_id, date, sales_amount, '1d');

-- View summary
SELECT * FROM anofox_fcst_ts_stats_summary('sales_stats');

-- Quality report
SELECT * FROM anofox_fcst_ts_quality_report('sales_stats', 30);
```

#### Handle Common Issues

```sql
-- Create sample raw sales data
CREATE TABLE sales_raw AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    CASE 
        WHEN RANDOM() < 0.05 THEN NULL
        ELSE 100 + 50 * SIN(2 * PI() * d / 7) + (RANDOM() * 20)
    END AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

-- Fill time gaps
CREATE TABLE sales_filled AS
SELECT 
    group_col AS product_id,
    date_col AS date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_gaps('sales_raw', product_id, date, sales_amount, '1d');

-- Remove constant series
CREATE TABLE sales_clean AS
SELECT * FROM anofox_fcst_ts_drop_constant('sales_filled', product_id, sales_amount);

-- Fill missing values
CREATE TABLE sales_complete AS
SELECT 
    product_id,
    date,
    value_col AS sales_amount
FROM anofox_fcst_ts_fill_nulls_forward('sales_clean', product_id, date, sales_amount);
```

### 2. Detect Seasonality

```sql
-- Create sample complete sales data
CREATE TABLE sales_complete AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + product_id * 20 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Automatically detect seasonal periods
SELECT 
    product_id,
    anofox_fcst_ts_detect_seasonality(LIST(sales_amount ORDER BY date)) AS detected_periods
FROM sales_complete
GROUP BY product_id;

-- Result:
-- | product_id | detected_periods | primary_period | is_seasonal |
-- |------------|------------------|----------------|-------------|
-- | P001       | [7, 30]          | 7              | true        |
```

### 3. Generate Forecasts

#### Single Series

```sql
-- Create sample sales data
CREATE TABLE sales_complete AS
SELECT 
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d);

SELECT * FROM anofox_fcst_ts_forecast(
    'sales_complete',
    date,
    sales_amount,
    'AutoETS',  -- Automatic model selection
    28,         -- 28 days ahead
    MAP{'seasonal_period': 7}
);
```

#### Multiple Series

```sql
-- Create sample multi-product sales data
CREATE TABLE sales_complete AS
SELECT 
    product_id,
    DATE '2023-01-01' + INTERVAL (d) DAY AS date,
    100 + 30 * SIN(2 * PI() * d / 7) + (RANDOM() * 10) AS sales_amount
FROM generate_series(0, 89) t(d)
CROSS JOIN (VALUES ('P001'), ('P002'), ('P003')) products(product_id);

SELECT * FROM anofox_fcst_ts_forecast_by(
    'sales_complete',
    product_id,     -- Parallel forecasting per product
    date,
    sales_amount,
    'AutoETS',
    28,
    MAP{'seasonal_period': 7, 'confidence_level': 0.95}
);
```

### 4. Evaluate Forecasts

#### Accuracy Metrics

```sql
-- Create sample forecasts data
CREATE TABLE forecasts AS
SELECT 
    product_id,
    forecast_step,
    DATE '2024-01-01' + INTERVAL (forecast_step) DAY AS date,
    100.0 + forecast_step * 2.0 AS point_forecast,
    90.0 + forecast_step * 1.5 AS lower,
    110.0 + forecast_step * 2.5 AS upper
FROM generate_series(1, 28) t(forecast_step)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Create actuals table
CREATE TABLE sales_actuals AS
SELECT 
    product_id,
    DATE '2024-01-01' + INTERVAL (forecast_step) DAY AS date,
    100.0 + forecast_step * 2.0 + (RANDOM() * 5) AS actual_sales
FROM generate_series(1, 28) t(forecast_step)
CROSS JOIN (VALUES (1), (2), (3)) products(product_id);

-- Assuming you have actual values for the forecast period
WITH actuals AS (
    SELECT product_id, date, actual_sales
    FROM sales_actuals
),
forecasts AS (
    SELECT product_id, date AS date, point_forecast
    FROM forecasts
)
SELECT 
    f.product_id,
    ROUND(anofox_fcst_ts_mae(LIST(a.actual_sales ORDER BY a.date), LIST(f.point_forecast ORDER BY f.date)), 2) AS mae,
    ROUND(anofox_fcst_ts_rmse(LIST(a.actual_sales ORDER BY a.date), LIST(f.point_forecast ORDER BY f.date)), 2) AS rmse,
    ROUND(anofox_fcst_ts_mape(LIST(a.actual_sales ORDER BY a.date), LIST(f.point_forecast ORDER BY f.date)), 2) AS mape
FROM forecasts f
JOIN actuals a ON f.product_id = a.product_id AND f.date = a.date
GROUP BY f.product_id;
```

#### Interval Coverage

```sql
-- Create sample forecast results
CREATE TABLE results AS
SELECT 
    1 AS forecast_step,
    100.0 AS actual,
    102.5 AS forecast,
    95.0 AS lower,
    110.0 AS upper
UNION ALL
SELECT 2, 105.0, 104.0, 96.0, 112.0
UNION ALL
SELECT 3, 103.0, 105.5, 97.0, 114.0
UNION ALL
SELECT 4, 108.0, 107.0, 98.0, 116.0
UNION ALL
SELECT 5, 106.0, 108.5, 99.0, 118.0;

-- Check if 95% intervals actually cover 95% of actuals
SELECT 
    ROUND(anofox_fcst_ts_coverage(LIST(actual ORDER BY forecast_step), LIST(lower ORDER BY forecast_step), LIST(upper ORDER BY forecast_step)) * 100, 1) AS coverage_pct
FROM results;

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
