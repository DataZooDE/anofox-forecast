# Forecasting

> Point forecasts and prediction intervals for time series

## Overview

The extension provides 32 forecasting models ranging from simple baselines to sophisticated state-space methods.

---

## Quick Start

Generate forecasts for multiple series with a single call:

```sql
-- Forecast 14 days ahead for all products using HoltWinters with weekly seasonality
SELECT * FROM ts_forecast_by(
    'sales_data',              -- source: table name (quoted string)
    product_id,                -- group_col: series identifier (unquoted)
    date,                      -- date_col: timestamp column (unquoted)
    revenue,                   -- target_col: value to forecast (unquoted)
    'HoltWinters',             -- method: forecasting model (seasonal)
    14,                        -- horizon: periods to forecast
    MAP{'seasonal_period': '7'}  -- params: weekly seasonality (required for seasonal models)
);
```

Compare multiple models:

```sql
-- Naive baseline (no params needed)
SELECT *, 'Naive' AS model FROM ts_forecast_by('sales', id, date, val, 'Naive', 7, MAP{})
UNION ALL
-- HoltWinters with weekly seasonality
SELECT *, 'HoltWinters' AS model FROM ts_forecast_by('sales', id, date, val, 'HoltWinters', 7, MAP{'seasonal_period': '7'});
```

### Handling Seasonality

> **Important:** Forecasting models do **not** auto-detect seasonality. You must detect it separately and pass `seasonal_period` explicitly.

**Step 1: Detect seasonality**
```sql
SELECT * FROM ts_detect_periods_by('daily_sales', product_id, date, value, MAP{});
-- Returns: primary_period = 7 (weekly pattern detected)
```

**Step 2: Use detected period in forecasting**
```sql
-- For forecasting
SELECT * FROM ts_forecast_by(
    'daily_sales', product_id, date, value,
    'AutoETS', 14,
    MAP{'seasonal_period': '7'}  -- Pass detected period explicitly
);

-- For backtesting
SELECT * FROM ts_backtest_auto_by(
    'daily_sales', product_id, date, value, 7, 5, '1d',
    MAP{'method': 'AutoETS', 'seasonal_period': '7'}
);
```

**Combined workflow** (detect and forecast in one query):
```sql
WITH detected AS (
    SELECT (periods).primary_period AS season
    FROM ts_detect_periods_by('daily_sales', product_id, date, value, MAP{})
    LIMIT 1
)
SELECT * FROM ts_forecast_by(
    'daily_sales', product_id, date, value,
    'HoltWinters', 14,
    MAP{'seasonal_period': (SELECT season FROM detected)::VARCHAR}
);
```

**Why explicit?** Auto-detection can produce unexpected results. By separating detection from forecasting, you can:
- Validate detected periods make business sense (e.g., 7 = weekly, 12 = monthly, 365 = yearly)
- Use domain knowledge to override detection
- Apply the same period consistently across models

### Complete Forecasting Workflow

End-to-end example showing data preparation through forecasting:

```sql
-- Step 1: Check data quality
SELECT id, (stats).length, (stats).n_nulls
FROM ts_stats_by('daily_sales', product_id, date, revenue, '1d');
-- Review: check for NULLs, gaps, and series length

-- Step 2: Detect seasonality (explicit control)
SELECT id, (periods).primary_period AS period
FROM ts_detect_periods_by('daily_sales', product_id, date, revenue, MAP{});
-- Example output: period = 7 (weekly pattern)

-- Step 3: Forecast with detected period
SELECT * FROM ts_forecast_by(
    'daily_sales',
    product_id, date, revenue,
    'HoltWinters', 14,
    MAP{'seasonal_period': '7'}
);
```

---

## API Styles

| API Style | Best For | Example |
|-----------|----------|---------|
| **Table Macros** | Most users; forecasting multiple series | `ts_forecast_by('sales', id, date, val, 'ETS', 12, MAP{})` |
| **Aggregate Functions** | Custom GROUP BY patterns | `ts_forecast_agg(date, value, 'ETS', 12, MAP{})` |

## Model Selection Guide

**For beginners:** Start with `Naive` or `SES` to establish baselines, then try `AutoETS` for automatic model selection.

| Data Characteristics | Recommended Models |
|---------------------|-------------------|
| No trend, no seasonality | `Naive`, `SES`, `SESOptimized` |
| Trend, no seasonality | `Holt`, `Theta`, `RandomWalkDrift` |
| Seasonality (single period) | `SeasonalNaive`, `HoltWinters`, `SeasonalES` |
| Multiple seasonalities | `MSTL`, `MFLES`, `TBATS` |
| Intermittent demand (many zeros) | `CrostonClassic`, `CrostonSBA`, `TSB` |
| Unknown characteristics | `AutoETS`, `AutoARIMA`, `AutoTheta` |

## Supported Models (32 Models)

### Automatic Selection Models (6)
| Model | Description | Optional Params |
|-------|-------------|-----------------|
| `AutoETS` | Automatic ETS model selection | *seasonal_period* |
| `AutoARIMA` | Automatic ARIMA model selection | *seasonal_period* |
| `AutoTheta` | Automatic Theta method selection | *seasonal_period* |
| `AutoMFLES` | Automatic MFLES selection | *seasonal_periods[]* |
| `AutoMSTL` | Automatic MSTL selection | *seasonal_periods[]* |
| `AutoTBATS` | Automatic TBATS selection | *seasonal_periods[]* |

### Basic Models (6)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `Naive` | Last value repeated | — | — |
| `SMA` | Simple Moving Average | — | *window* (default: 5) |
| `SeasonalNaive` | Last season repeated | **seasonal_period** | — |
| `SES` | Simple Exponential Smoothing | — | *alpha* (default: 0.3) |
| `SESOptimized` | Optimized SES | — | — |
| `RandomWalkDrift` | Random walk with drift | — | — |

### Exponential Smoothing Models (4)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `Holt` | Holt's linear trend method | — | *alpha*, *beta* |
| `HoltWinters` | Holt-Winters seasonal method | **seasonal_period** | *alpha*, *beta*, *gamma* |
| `SeasonalES` | Seasonal Exponential Smoothing | **seasonal_period** | *alpha*, *gamma* |
| `SeasonalESOptimized` | Optimized Seasonal ES | **seasonal_period** | — |

### Theta Methods (5)
| Model | Description | Optional |
|-------|-------------|----------|
| `Theta` | Standard Theta method | *seasonal_period*, *theta* |
| `OptimizedTheta` | Optimized Theta method | *seasonal_period* |
| `DynamicTheta` | Dynamic Theta method | *seasonal_period*, *theta* |
| `DynamicOptimizedTheta` | Dynamic Optimized Theta | *seasonal_period* |
| `AutoTheta` | Automatic Theta selection | *seasonal_period* |

### State Space & ARIMA Models (4)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `ETS` | Error-Trend-Seasonal model | — | *seasonal_period*, *model* |
| `AutoETS` | Automatic ETS selection | — | *seasonal_period* |
| `ARIMA` | ARIMA model | **p**, **d**, **q** | *P*, *D*, *Q*, *s* |
| `AutoARIMA` | Automatic ARIMA selection | — | *seasonal_period* |

### Multiple Seasonality Models (6)
| Model | Description | Required | Optional |
|-------|-------------|----------|----------|
| `MFLES` | Multiple Frequency LES | **seasonal_periods[]** | *iterations* |
| `AutoMFLES` | Automatic MFLES | — | *seasonal_periods[]* |
| `MSTL` | Multiple Seasonal-Trend Loess | **seasonal_periods[]** | *stl_method* |
| `AutoMSTL` | Automatic MSTL | — | *seasonal_periods[]* |
| `TBATS` | Trigonometric BATS | **seasonal_periods[]** | *use_box_cox* |
| `AutoTBATS` | Automatic TBATS | — | *seasonal_periods[]* |

### Intermittent Demand Models (6)
| Model | Description | Optional |
|-------|-------------|----------|
| `CrostonClassic` | Classic Croston's method | — |
| `CrostonOptimized` | Optimized Croston's method | — |
| `CrostonSBA` | Syntetos-Boylan Approximation | — |
| `ADIDA` | Aggregate-Disaggregate IDA | — |
| `IMAPA` | Intermittent Multiple Aggregation | — |
| `TSB` | Teunter-Syntetos-Babai method | *alpha_d*, *alpha_p* |

---

## Table Macros

### ts_forecast

Generate forecasts for a single series.

**Signature:**
```sql
ts_forecast(table_name, date_col, target_col, method, horizon, params) → TABLE
```

**Example:**
```sql
SELECT * FROM ts_forecast('sales', date, amount, 'Naive', 12, MAP{});
```

---

### ts_forecast_by

Generate forecasts for multiple time series grouped by an identifier. This is the **primary forecasting function**.

**Signature:**
```sql
ts_forecast_by(table_name, group_col, date_col, target_col, method, horizon, params) → TABLE
```

**Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `table_name` | VARCHAR | Source table name (quoted string) |
| `group_col` | IDENTIFIER | Column for grouping series (unquoted) |
| `date_col` | IDENTIFIER | Date/timestamp column (unquoted) |
| `target_col` | IDENTIFIER | Target value column (unquoted) |
| `method` | VARCHAR | Forecasting method (case-sensitive) |
| `horizon` | INTEGER | Number of periods to forecast |
| `params` | MAP or STRUCT | Model parameters |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `group_col` | ANY | Series identifier |
| `ds` | TIMESTAMP | Forecast timestamp |
| `forecast` | DOUBLE | Point forecast |
| `lower` | DOUBLE | Lower prediction interval |
| `upper` | DOUBLE | Upper prediction interval |

**Examples:**
```sql
-- HoltWinters with weekly seasonality (guaranteed seasonal model)
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'HoltWinters', 12,
    MAP{'seasonal_period': '7'});

-- AutoETS considers seasonal models when seasonal_period provided
-- (may still select non-seasonal if it fits better)
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'AutoETS', 12,
    MAP{'seasonal_period': '7'});

-- MSTL with multiple seasonal periods (array as JSON string)
SELECT * FROM ts_forecast_by('sales', id, date, val, 'MSTL', 30,
    MAP{'seasonal_periods': '[7, 365]'});

-- Naive baseline (no seasonal_period needed)
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'Naive', 12, MAP{});
```

---

### ts_forecast_exog_by

Multi-series forecasting with exogenous variables. This is the **primary exogenous forecasting function**.

**Signature:**
```sql
ts_forecast_exog_by(table_name, group_col, date_col, target_col, x_cols, future_table, future_date_col, future_x_cols, model, horizon, params, frequency) → TABLE
```

**Supported Models with Exogenous:**
| Base Model | With Exog | Description |
|------------|-----------|-------------|
| `ARIMA` | `ARIMAX` | ARIMA with exogenous regressors |
| `AutoARIMA` | `ARIMAX` | Auto-selected ARIMA with exogenous |
| `OptimizedTheta` | `ThetaX` | Theta method with exogenous |
| `MFLES` | `MFLESX` | MFLES with exogenous regressors |

**Example:**
```sql
-- Create historical data with exogenous variable
CREATE TABLE sales_history AS
SELECT product_id, date, revenue, temperature
FROM sales_with_weather;

-- Create future exogenous data (must cover forecast horizon)
CREATE TABLE future_weather AS
SELECT product_id, date, temperature
FROM weather_forecast;

-- Forecast with exogenous regressor
SELECT * FROM ts_forecast_exog_by(
    'sales_history',       -- historical data table
    product_id,            -- group column
    date,                  -- date column
    revenue,               -- target column
    ['temperature'],       -- exogenous columns (historical)
    'future_weather',      -- future exogenous table
    date,                  -- future date column
    ['temperature'],       -- future exogenous columns
    'AutoARIMA',           -- model
    7,                     -- horizon
    MAP{},                 -- params
    '1d'                   -- frequency
);
```

---

### ts_forecast_exog

Single-series forecasting with exogenous variables.

**Signature:**
```sql
ts_forecast_exog(table_name, date_col, target_col, x_cols, future_table, model, horizon, params) → TABLE
```

**Example:**
```sql
SELECT * FROM ts_forecast_exog(
    'sales', date, amount,
    'temperature,promotion',
    'future_exog',
    'AutoARIMA', 3, MAP{}
);
```

---

## Aggregate Function

### ts_forecast_agg

Aggregate function for generating forecasts with GROUP BY.

**Signature:**
```sql
ts_forecast_agg(date_col, value_col, method, horizon, params) → STRUCT
```

**Returns:**
```sql
STRUCT(
    forecast_step      INTEGER[],
    forecast_timestamp TIMESTAMP[],
    point_forecast     DOUBLE[],
    lower_90           DOUBLE[],
    upper_90           DOUBLE[],
    model_name         VARCHAR,
    insample_fitted    DOUBLE[],
    error_message      VARCHAR
)
```

**Example:**
```sql
SELECT
    product_id,
    ts_forecast_agg(ts, value, 'ETS', 12, MAP{}) AS forecast
FROM sales
GROUP BY product_id;
```

---

*See also: [Cross-Validation](06-cross-validation.md) | [Evaluation Metrics](07-evaluation-metrics.md)*
