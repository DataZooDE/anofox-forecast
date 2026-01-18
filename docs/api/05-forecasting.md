# Forecasting

> Point forecasts and prediction intervals for time series

## Overview

The extension provides 32 forecasting models ranging from simple baselines to sophisticated state-space methods.

---

## Quick Start

Generate forecasts for multiple series with a single call:

```sql
-- Forecast 14 days ahead for all products using AutoETS
SELECT * FROM ts_forecast_by(
    'sales_data',
    product_id,
    date,
    revenue,
    'AutoETS',
    14,
    MAP{}
);
```

Compare multiple models:

```sql
-- Naive baseline
SELECT *, 'Naive' AS model FROM ts_forecast_by('sales', id, date, val, 'Naive', 7, MAP{})
UNION ALL
-- AutoETS
SELECT *, 'AutoETS' AS model FROM ts_forecast_by('sales', id, date, val, 'AutoETS', 7, MAP{});
```

For seasonal data (e.g., weekly patterns):

```sql
SELECT * FROM ts_forecast_by(
    'daily_sales', product_id, date, value,
    'HoltWinters', 14,
    {'seasonal_period': 7}
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
-- Basic forecast with AutoETS
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'AutoETS', 12, MAP{});

-- With STRUCT params (mixed types)
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'HoltWinters', 12,
    {'seasonal_period': 7, 'alpha': 0.2});

-- MSTL with multiple seasonal periods
SELECT * FROM ts_forecast_by('sales', id, date, val, 'MSTL', 30,
    MAP{'seasonal_periods': '[7, 365]'});
```

---

### ts_forecast_exog

Single-series forecasting with exogenous variables.

**Signature:**
```sql
ts_forecast_exog(table_name, date_col, target_col, x_cols, future_table, model, horizon, params) → TABLE
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
SELECT * FROM ts_forecast_exog(
    'sales', date, amount,
    'temperature,promotion',
    'future_exog',
    'AutoARIMA', 3, MAP{}
);
```

---

### ts_forecast_exog_by

Multi-series forecasting with exogenous variables.

**Signature:**
```sql
ts_forecast_exog_by(table_name, group_col, date_col, target_col, x_cols, future_table, model, horizon, params) → TABLE
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

## ETS Model Specification

The `model` parameter for ETS accepts a 3-4 character string:

| Position | Component | Values |
|----------|-----------|--------|
| 1st | Error | `A` (Additive), `M` (Multiplicative) |
| 2nd | Trend | `N` (None), `A` (Additive), `M` (Multiplicative) |
| 3rd (optional) | Damped | `d` (damped trend) |
| Last | Seasonal | `N` (None), `A` (Additive), `M` (Multiplicative) |

**Common Models:**
| Spec | Description |
|------|-------------|
| `ANN` | Simple exponential smoothing |
| `AAN` | Holt's linear method |
| `AAA` | Additive Holt-Winters |
| `MAM` | Multiplicative Holt-Winters |
| `AAdN` | Damped trend, no seasonality |
| `MAdM` | Damped multiplicative Holt-Winters |

---

*See also: [Cross-Validation](06-cross-validation.md) | [Evaluation Metrics](07-evaluation-metrics.md)*
