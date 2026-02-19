# Getting Started

> Quick introduction to the Anofox Forecast Extension

**Use this guide to:**
- Install and load the extension in DuckDB
- Run your first forecast with sample data
- Understand time series data structure (group, date, value)
- Choose between three API styles (table macros, scalar functions, aggregates)
- Learn essential patterns for grouped forecasting

## Installation

```sql
-- Install from DuckDB Community Extensions
INSTALL anofox_forecast FROM community;
LOAD anofox_forecast;
```

## Your First Forecast

```sql
-- Create sample data
CREATE TABLE sales AS
SELECT
    'product_' || (i % 3) AS product_id,
    '2024-01-01'::DATE + (i * INTERVAL '1 day') AS date,
    100.0 + (i % 7) * 10 + random() * 5 AS quantity
FROM generate_series(0, 99) AS t(i);

-- Generate 14-day forecasts for each product
SELECT * FROM ts_forecast_by(
    'sales',           -- table name
    product_id,        -- group column
    date,              -- date column
    quantity,          -- value column
    'AutoETS',         -- forecasting model
    14,                -- forecast horizon
    '1d'               -- frequency: daily
);
```

## Key Concepts

### Time Series Structure

Time series data needs three components:
1. **Group identifier** - identifies each series (e.g., product_id, store_id)
2. **Date/timestamp** - temporal ordering
3. **Value** - the metric to forecast

### API Styles

The extension offers three ways to work with data:

```sql
-- 1. Table Macros (recommended for most cases)
SELECT * FROM ts_forecast_by('sales', product_id, date, quantity, 'AutoETS', 12, '1d');

-- 2. Scalar Functions (for array operations)
SELECT product_id, ts_stats(LIST(quantity ORDER BY date)) AS stats
FROM sales GROUP BY product_id;

-- 3. Aggregate Functions (for custom GROUP BY)
SELECT product_id, ts_forecast_agg(date, quantity, 'AutoETS', 12, MAP{}) AS forecast
FROM sales GROUP BY product_id;
```

### Parameter Syntax

Parameters can be passed as MAP (string values) or STRUCT (mixed types):

```sql
-- STRUCT (recommended)
SELECT * FROM ts_forecast_by('sales', id, date, val, 'HoltWinters', 12, '1d',
    {'seasonal_period': 7, 'alpha': 0.2});

-- MAP (legacy)
SELECT * FROM ts_forecast_by('sales', id, date, val, 'HoltWinters', 12, '1d',
    MAP{'seasonal_period': '7', 'alpha': '0.2'});
```

## Common Workflows

### 1. Explore Your Data

```sql
-- Compute statistics per series
SELECT * FROM ts_stats('sales', product_id, date, quantity);

-- Check data quality
SELECT
    product_id,
    (ts_data_quality(LIST(quantity ORDER BY date))).overall_score AS quality
FROM sales
GROUP BY product_id;
```

### 2. Detect Seasonality

```sql
-- Detect seasonal patterns (e.g., weekly = 7, monthly = 30)
SELECT * FROM ts_detect_periods_by('sales', product_id, date, quantity, MAP{});
-- Returns: primary_period = 7 (weekly pattern)
```

### 3. Evaluate Models

```sql
-- Backtest with detected seasonal period
SELECT
    model_name,
    AVG(abs_error) AS mae,
    AVG(fold_metric_score) AS rmse
FROM ts_backtest_auto_by(
    'sales', product_id, date, quantity,
    7,       -- 7-day horizon
    3,       -- 3 folds
    '1d',    -- daily frequency
    {'method': 'AutoETS', 'seasonal_period': 7}  -- Use detected period
)
GROUP BY model_name;
```

### 4. Generate Forecasts

```sql
-- Production forecasts with seasonal period
CREATE TABLE forecasts AS
SELECT * FROM ts_forecast_by(
    'sales', product_id, date, quantity,
    'AutoETS', 30, '1d',
    {'seasonal_period': 7}  -- Same period as backtesting
);
```

## Next Steps

- [Model Selection Guide](02-model-selection.md) - Choose the right forecasting model
- [Cross-Validation Guide](03-cross-validation.md) - Properly evaluate your forecasts
- [API Reference](../API_REFERENCE.md) - Complete function documentation
