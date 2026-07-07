# Getting Started

> Quick introduction to the Anofox Forecast Extension

**Use this guide to:**
- Install and load the extension in DuckDB
- Run your first forecast with sample data
- Understand time series data structure (group, date, value)
- Choose between three API styles (table macros, table functions, aggregates)
- Learn essential patterns for grouped forecasting

## Installation

```sql
-- Install from DuckDB Community Extensions
INSTALL anofox_forecast FROM community;
LOAD anofox_forecast;

-- The json extension is required for a few workflows (period detection,
-- backtesting). Enable auto-load so it comes up on demand:
SET autoinstall_known_extensions = 1;
SET autoload_known_extensions = 1;
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
    'sales',           -- table name (quoted string)
    product_id,        -- group column (unquoted identifier)
    date,              -- date column (unquoted identifier)
    quantity,          -- value column (unquoted identifier)
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

-- 2. Table Functions with LIST(...) (for pre-aggregated arrays)
SELECT * FROM ts_stats('sales', product_id, date, quantity, '1d');

-- 3. Aggregate Functions (for custom GROUP BY)
SELECT product_id, ts_forecast_agg(date, quantity, 'AutoETS', 12, MAP{}) AS forecast
FROM sales GROUP BY product_id;
```

### Parameter Syntax

Parameters can be passed as MAP (all values quoted as strings) or STRUCT (mixed types):

```sql
-- STRUCT (recommended — keeps numeric params typed)
SELECT * FROM ts_forecast_by('sales', product_id, date, quantity, 'HoltWinters', 12, '1d',
    {seasonal_period: 7});

-- MAP (all values must be strings)
SELECT * FROM ts_forecast_by('sales', product_id, date, quantity, 'HoltWinters', 12, '1d',
    MAP{'seasonal_period': '7'});
```

## Common Workflows

### 1. Explore Your Data

```sql
-- Compute statistics per series (table function)
SELECT * FROM ts_stats('sales', product_id, date, quantity, '1d');

-- Check data quality (table function)
SELECT * FROM ts_data_quality('sales', product_id, date, quantity, 14, '1d');

-- ...or use the aggregate variants with a custom GROUP BY
SELECT
    product_id,
    ts_data_quality_agg(date, quantity).overall_score AS quality
FROM sales
GROUP BY product_id;
```

### 2. Detect Seasonality

```sql
-- Detect seasonal patterns (e.g., weekly = 7, monthly = 30).
-- Requires the json extension; auto-loaded when SET autoload_known_extensions=1.
SELECT * FROM ts_detect_periods_by('sales', product_id, date, quantity, MAP{});
-- Returns: primary_period = 7 (weekly pattern)
```

### 3. Backtest with Cross-Validation

Backtesting uses the two-step CV workflow: split the data into rolling
train/test folds, then generate a forecast per fold.

```sql
-- Step 1: Create 3 folds with a 7-day test window each
CREATE OR REPLACE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by(
    'sales', product_id, date, quantity,
    3,       -- 3 folds
    7,       -- 7-day horizon
    MAP{}
);

-- Step 2: Fit AutoETS on each fold's train partition and forecast the test partition
CREATE OR REPLACE TABLE cv_forecasts AS
SELECT * FROM ts_cv_forecast_by(
    'cv_folds', product_id, date, quantity,
    'AutoETS',
    MAP{'seasonal_period': '7'}   -- Use detected period
);

-- Step 3: Compute error metrics per series and fold
SELECT
    product_id,
    fold_id,
    ts_rmse(LIST(y ORDER BY date), LIST(yhat ORDER BY date)) AS rmse,
    ts_mae(LIST(y ORDER BY date), LIST(yhat ORDER BY date))  AS mae
FROM cv_forecasts
GROUP BY product_id, fold_id
ORDER BY product_id, fold_id;
```

See the [Cross-Validation Guide](03-cross-validation.md) for expanding/fixed/sliding
windows, gap/embargo, and custom cutoffs.

### 4. Generate Forecasts

```sql
-- Production forecasts with seasonal period
CREATE OR REPLACE TABLE forecasts AS
SELECT * FROM ts_forecast_by(
    'sales', product_id, date, quantity,
    'AutoETS', 30, '1d',
    MAP{'seasonal_period': '7'}  -- Same period as backtesting
);
```

## Next Steps

- [Model Selection Guide](02-model-selection.md) - Choose the right forecasting model
- [Cross-Validation Guide](03-cross-validation.md) - Properly evaluate your forecasts
- [API Reference](../API_REFERENCE.md) - Complete function documentation
