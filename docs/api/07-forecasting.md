# Forecasting

> Point forecasts and prediction intervals for time series

## Overview

The extension provides 36 forecasting models ranging from simple baselines to sophisticated state-space methods, classical volatility models, and multivariate Vector Autoregression.

**Use this document to:**
- Generate point forecasts and prediction intervals for single or multiple series
- Choose from 37 models: baselines (Naive, SMA), exponential smoothing (ETS, Holt-Winters), state-space (ARIMA, Kalman), classical (GARCH), ensemble (AutoEnsemble), multi-seasonal (MSTL, TBATS), intermittent demand (Croston, TSB), distributional (Laplace), and multivariate (VAR)
- Use automatic model selection (AutoETS, AutoARIMA, AutoTheta, AutoEnsemble) when unsure
- Incorporate exogenous variables with supported models
- Understand the detect-then-forecast workflow for seasonal data

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
    '1d',                      -- frequency: time step between observations
    MAP{'seasonal_period': '7'}  -- params: weekly seasonality (required for seasonal models)
);
```

Compare multiple models:

```sql
-- Naive baseline (no params needed)
SELECT *, 'Naive' AS model FROM ts_forecast_by('sales', id, date, val, 'Naive', 7, '1d', MAP{})
UNION ALL
-- HoltWinters with weekly seasonality
SELECT *, 'HoltWinters' AS model FROM ts_forecast_by('sales', id, date, val, 'HoltWinters', 7, '1d', MAP{'seasonal_period': '7'});
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
    'AutoETS', 14, '1d',
    MAP{'seasonal_period': '7'}  -- Pass detected period explicitly
);

-- For backtesting (two-step cross-validation)
CREATE TABLE cv_folds AS
SELECT * FROM ts_cv_folds_by('daily_sales', product_id, date, value, 5, 7, MAP{});
SELECT * FROM ts_cv_forecast_by(
    'cv_folds', product_id, date, value,
    'AutoETS', MAP{'seasonal_period': '7'}
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
    'HoltWinters', 14, '1d',
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
    'HoltWinters', 14, '1d',
    MAP{'seasonal_period': '7'}
);
```

---

## Model Selection Guide

**For beginners:** Start with `Naive` or `SES` to establish baselines, then try `AutoETS` or `AutoEnsemble` for automatic model selection.

| Data Characteristics | Recommended Models |
|---------------------|-------------------|
| No trend, no seasonality | `Naive`, `SES`, `SESOptimized` |
| Trend, no seasonality | `Holt`, `Theta`, `RandomWalkDrift` |
| Seasonality (single period) | `SeasonalNaive`, `HoltWinters`, `SeasonalES`, `Laplace` |
| Multiple seasonalities | `MSTL`, `MFLES`, `TBATS` |
| Intermittent demand (many zeros) | `CrostonClassic`, `CrostonSBA`, `TSB`, `Laplace` (with `auto_aid`) |
| Unknown characteristics | `AutoETS`, `AutoARIMA`, `AutoTheta`, `AutoEnsemble`, `Laplace` |
| Ensemble of ARIMA/ETS/Theta | `AutoEnsemble` |
| Streaming / distributional | `Laplace` |

## Supported Models (33 Models)

### Automatic Selection Models (7)
| Model | Description | Optional Params |
|-------|-------------|-----------------|
| `AutoETS` | Automatic ETS model selection | *seasonal_period* |
| `AutoARIMA` | Automatic ARIMA model selection | *seasonal_period* |
| `AutoTheta` | Automatic Theta method selection | *seasonal_period* |
| `AutoEnsemble` | Auto-fit ARIMA/ETS/Theta, rank by MSE, combine top-K | *top_k*, *combination_method*, *seasonal_period* |
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

### Distributional Models (1)

Streaming likelihood-weighted mixture of leaves (EMA / drift / AR(1) / damped-Holt + optional seasonal / distribution-family leaves). Point + interval forecasts; the full mixture parameters will be exposed by the upcoming `ts_forecast_dist_by` (PR B).

| Model | Description | Optional |
|-------|-------------|----------|
| `Laplace` | Streaming distributional forecaster with three zero-config selectors (`auto` / `auto_aid` / `skaters`) | *seasonal_period*, *laplace_variant*, *laplace_seasonal_batch_init* |

**Variants** (`laplace_variant`):
- `auto` (default) — balanced for smooth / continuous series with adequate history
- `auto_aid` — AID-based distribution-family selection; best for retail SKU / intermittent counts
- `skaters` — fuller skaters ensemble (multi-h scoring, stacking, larger leaf set); slower, more robust

**Batch init** (`laplace_seasonal_batch_init: 1`) initialises the seasonal-EMA phase levels from the last training cycle. Safe on stationary or amplitude-declining seasonal series; avoid on growing amplitude or phase-shifted seasonality (the softmax abandons the seasonal-EMA leaf and the forecast collapses to flat).

See [reference/models/distributional/laplace.md](../reference/models/distributional/laplace.md) for the full trade-off table and worked examples.

---

## Table Macros

### ts_forecast_by

Generate forecasts for multiple time series grouped by an identifier. This is the **primary forecasting function**.

**Signature:**
```sql
ts_forecast_by(table_name, group_col, date_col, target_col, method, horizon, frequency, params?) → TABLE
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
| `frequency` | VARCHAR | Time step between observations (e.g., `'1d'`, `'1h'`, `'1mo'`) |
| `params` | MAP or STRUCT | Model parameters (optional) |

**Returns:**
| Column | Type | Description |
|--------|------|-------------|
| `<group_col>` | (same as input) | Series identifier |
| `<date_col>` | (same as input) | Forecast timestamp |
| `yhat` | DOUBLE | Point forecast |
| `yhat_lower` | DOUBLE | Lower prediction interval |
| `yhat_upper` | DOUBLE | Upper prediction interval |

**Examples:**
```sql
-- HoltWinters with weekly seasonality (guaranteed seasonal model)
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'HoltWinters', 12, '1d',
    MAP{'seasonal_period': '7'});

-- AutoETS considers seasonal models when seasonal_period provided
-- (may still select non-seasonal if it fits better)
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'AutoETS', 12, '1d',
    MAP{'seasonal_period': '7'});

-- MSTL with multiple seasonal periods (array as JSON string)
SELECT * FROM ts_forecast_by('sales', id, date, val, 'MSTL', 30, '1d',
    MAP{'seasonal_periods': '[7, 365]'});

-- Naive baseline (no seasonal_period needed)
SELECT * FROM ts_forecast_by('sales', product_id, date, amount, 'Naive', 12, '1d', MAP{});
```

---

### ts_forecast_exog_by

Multi-series forecasting with exogenous variables. This is the **primary exogenous forecasting function**.

**Signature:**
```sql
ts_forecast_exog_by(table_name, group_col, date_col, target_col, x_cols, future_table, future_date_col, future_x_cols, frequency, model, horizon, params?) → TABLE
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
    '1d',                  -- frequency
    'AutoARIMA',           -- model
    7,                     -- horizon
    MAP{}                  -- params (optional)
);
```

---

## Panel / Global Forecasting (`ts_forecast_panel_by`)

Global cross-series learners that fit **one shared model across all series simultaneously** and predict per-series forecasts. Unlike `ts_forecast_by` (N independent fits), panel models pool the parameter optimization across the full panel — making them faster and better-regularized when individual series are short.

**Three supported methods:**

| Method | Description | When to Use |
|--------|-------------|-------------|
| `'GlobalETS'` | Pooled ETS with automatic spec selection (`GlobalAutoETS`) | Many related series with shared seasonal dynamics |
| `'GlobalTheta'` | Pooled Theta (Standard Theta Method, theta=2.0) | Trended panels; no seasonal config needed |
| `'GlobalCroston'` | Pooled Croston (Classic or SBA bias correction) | Intermittent/spare-parts panels with many zeros |

### Signature

```sql
ts_forecast_panel_by(
    source_table  VARCHAR,     -- table name (quoted string)
    group_col     IDENTIFIER,  -- series identifier (unquoted)
    date_col      IDENTIFIER,  -- date/timestamp column (unquoted)
    target_col    IDENTIFIER,  -- value column (unquoted)
    method        VARCHAR,     -- 'GlobalETS' | 'GlobalTheta' | 'GlobalCroston'
    horizon       INTEGER,     -- periods to forecast
    frequency     VARCHAR,     -- '1d', '1h', '1mo', ...
    params        MAP{}        -- optional; see per-method params below
) → TABLE(group_col, forecast_step INT, date_col TIMESTAMP, yhat DOUBLE, model_name VARCHAR)
```

### Ragged panel handling

Series with different lengths or start dates are automatically handled:
1. A shared date grid is built as the **union of all dates** across the panel, at the declared `frequency`.
2. Each series is aligned to the shared grid; gaps are filled with NaN and then imputed via linear interpolation.
3. Series with **fewer than 10 valid observations** after alignment are **dropped** and emitted as `DROPPED: too_short` rows (not as errors).
4. At least **3 series** must survive the drop step for the global fit to proceed.

### Point forecasts only (v1)

`ts_forecast_panel_by` returns point forecasts only — `yhat_lower` / `yhat_upper` are not available in this release. Prediction intervals via conformal prediction are planned for a future increment (D-Area3).

### Method-specific params

**GlobalETS:**
```sql
MAP {
    'seasonal_period': '7',    -- period for seasonal ETS (0 or omit = non-seasonal only)
    'model_pool': 'Reduced'    -- 'Reduced' (8 candidates, default) or 'Complete' (19)
}
```

**GlobalTheta:** No params accepted (seasonal_period is ignored).

**GlobalCroston:**
```sql
MAP {
    'croston_variant': 'SBA'   -- 'Classic' (default) or 'SBA' (bias correction)
}
```

### Examples

```sql
-- GlobalETS: weekly seasonal panel (verified end-to-end)
SELECT uid AS series, forecast_step, ROUND(yhat, 2) AS yhat, model_name
FROM ts_forecast_panel_by(
    'seasonal_panel',
    uid,
    ds,
    y,
    'GlobalETS',
    7,
    '1d',
    MAP {'seasonal_period': '7'}
)
ORDER BY series, forecast_step;

-- GlobalTheta: trended panel, no seasonal config (verified end-to-end)
SELECT product_id, forecast_step, ds, ROUND(yhat, 2) AS yhat, model_name
FROM ts_forecast_panel_by(
    'panel_sales',
    product_id,
    ds,
    y,
    'GlobalTheta',
    14,
    '1d'
)
ORDER BY product_id, forecast_step;

-- GlobalCroston SBA: intermittent demand panel (verified end-to-end)
SELECT item_id, forecast_step, ds, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_panel_by(
    'panel_intermittent',
    item_id,
    ds,
    qty,
    'GlobalCroston',
    6,
    '1d',
    MAP {'croston_variant': 'SBA'}
)
ORDER BY item_id, forecast_step;

-- Method comparison (GlobalETS vs GlobalTheta on same panel)
SELECT 'GlobalETS' AS method, product_id, forecast_step, ROUND(yhat, 2) AS yhat
FROM ts_forecast_panel_by('panel_sales', product_id, ds, y, 'GlobalETS', 7, '1d')
UNION ALL
SELECT 'GlobalTheta', product_id, forecast_step, ROUND(yhat, 2)
FROM ts_forecast_panel_by('panel_sales', product_id, ds, y, 'GlobalTheta', 7, '1d')
ORDER BY product_id, method, forecast_step;
```

### Per-method reference

- [GlobalETS](../reference/models/exponential-smoothing/global_ets.md) — pooled ETS with optional seasonality
- [GlobalTheta](../reference/models/theta/global_theta.md) — pooled Theta, minimal config
- [GlobalCroston](../reference/models/intermittent/global_croston.md) — pooled Croston for intermittent demand

---

## Classical Models — GARCH and Kalman

Univariate models for conditional volatility (GARCH) and state-space smoothing (Kalman),
accessible via the standard `ts_forecast_by` surface with `method = 'GARCH'` or `method = 'Kalman'`.

### GARCH — Conditional Volatility Forecasting

> **forecast_value (yhat) is conditional VOLATILITY (std-dev), not variance.**
> `yhat = sqrt(forecast_variance(h))`. Square it for variance: `yhat * yhat`.

Use GARCH on **returns** (first differences), not raw price levels. GARCH models volatility
clustering in financial time series: periods of high volatility tend to be followed by high
volatility, and low by low.

**Default:** GARCH(1,1). Override `p` and `q` via the `params` MAP.

```sql
-- GARCH(1,1) — conditional volatility on returns (verified end-to-end)
CREATE OR REPLACE TABLE returns AS
    SELECT 'Asset_A' AS asset_id,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           0.5 * SIN(i * 0.7) + 0.3 * COS(i * 0.3) AS y
    FROM range(40) t(i);

SELECT
    asset_id,
    forecast_step,
    ds,
    ROUND(yhat, 6) AS conditional_volatility,
    model_name
FROM ts_forecast_by('returns', asset_id, ds, y, 'GARCH', 7, '1d')
ORDER BY asset_id, forecast_step;

-- GARCH(1,1) — explicit p=1, q=1 via params
SELECT asset_id, forecast_step, ds, ROUND(yhat, 6) AS conditional_volatility, model_name
FROM ts_forecast_by(
    'returns', asset_id, ds, y, 'GARCH', 7, '1d',
    params := MAP{'garch_p':'1','garch_q':'1'}
)
ORDER BY asset_id, forecast_step;
```

**Minimum observations:** p + q + 10 (GARCH(1,1): 12 minimum).

See [GARCH reference](../reference/models/classical/garch.md) for full parameter docs and pitfalls.

### Kalman — State-Space Smoothing + h-Step Forecasting

Two state-space specifications via the `kalman_model` param:

| Spec | `kalman_model` value | Description |
|------|----------------------|-------------|
| Local level | `'local_level'` (default) | Random walk + noise; flat h-step forecast |
| Local linear trend | `'local_linear_trend'` | Level + slope; linearly growing/shrinking forecast |

```sql
-- Kalman local_level (default) — verified end-to-end
CREATE OR REPLACE TABLE sales AS
    SELECT 'Product_X' AS product_id,
           DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           100.0 + i * 0.8 + 5.0 * SIN(2 * PI() * i / 7.0) AS y
    FROM range(30) t(i);

SELECT product_id, forecast_step, ds, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by('sales', product_id, ds, y, 'Kalman', 7, '1d')
ORDER BY product_id, forecast_step;

-- Kalman local_linear_trend — captures trend direction
SELECT product_id, forecast_step, ds, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by(
    'sales', product_id, ds, y, 'Kalman', 7, '1d',
    params := MAP{'kalman_model': 'local_linear_trend'}
)
ORDER BY product_id, forecast_step;
```

See [Kalman reference](../reference/models/state-space/kalman.md) for full docs.

---

## Ensemble Forecasting — AutoEnsemble

`AutoEnsemble` automatically fits three candidate models (AutoARIMA, AutoETS, AutoTheta),
ranks them by in-sample MSE, and combines the top-K members into a single forecast using
the specified `combination_method`. It is accessible via `ts_forecast_by` with
`method = 'AutoEnsemble'`.

> **Point forecasts only (Phase 4):** `yhat_lower` / `yhat_upper` are `NULL`.
> Ensemble prediction intervals are planned for Phase 6 (EPI-01).

### Parameters

| Param | Type | Default | Description |
|-------|------|---------|-------------|
| `top_k` | INTEGER | `3` | Number of top-ranked candidates to combine |
| `combination_method` | VARCHAR | `'mean'` | Blend strategy (see table below) |
| `seasonal_period` | INTEGER | `0` | Period passed to all three candidate models; `0` = non-seasonal |

### combination_method strings

| SQL string(s) | Strategy |
|---|---|
| `''`, `'mean'` | Unweighted arithmetic mean of member forecasts |
| `'median'` | Coordinate-wise median; robust to outlier members |
| `'weighted_mse'`, `'weightedmse'`, `'weighted-mse'` | Inverse-MSE weighting |
| `'inverse_aic'`, `'inverseaic'`, `'inverse-aic'`, `'aic'` | AIC-based weighting |
| `'stacking'`, `'stack'` | Ridge-regularised stacking weights from in-sample holdout |
| `'horizon_adaptive'`, `'horizonadaptive'`, `'horizon-adaptive'`, `'adaptive'` | Per-step weights from rolling-origin errors |

All strings are case-insensitive. The string `'custom'` is **not** accepted (ENS-F1, deferred).

### Examples (verified end-to-end)

```sql
-- Basic usage — default mean combination, non-seasonal (verified end-to-end)
CREATE OR REPLACE TABLE sales AS
SELECT
    'Product_A' AS product_id,
    '2020-01-01'::DATE + INTERVAL (i - 1) DAY AS ds,
    10.0 + i * 0.5 AS y
FROM generate_series(1, 60) t(i);

SELECT product_id, forecast_step, ds, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by('sales', product_id, ds, y, 'AutoEnsemble', 5, '1d')
ORDER BY product_id, forecast_step;

-- Weighted MSE combination — better-fitting members get higher weight
SELECT product_id, forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by(
    'sales', product_id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'weighted_mse', seasonal_period: 0}
)
ORDER BY product_id, forecast_step;

-- Stacking combination — holdout-fitted weights
SELECT product_id, forecast_step, ROUND(yhat, 4) AS yhat, model_name
FROM ts_forecast_by(
    'sales', product_id, ds, y, 'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'stacking', seasonal_period: 0}
)
ORDER BY product_id, forecast_step;
```

See [AutoEnsemble reference](../reference/models/ensemble/autoensemble.md) for the full
parameter docs, all six `combination_method` strings + aliases, limitations, and choosing
between combination strategies.

---

## Multivariate Forecasting (`ts_forecast_var_by`)

VAR (Vector Autoregression) fits **one model across K variables simultaneously**,
capturing cross-variable dynamics. Unlike `ts_forecast_by` (one model per series per group),
`ts_forecast_var_by` uses all variable columns together in a single multivariate fit.

> **v1 constraint:** Single-panel only (no `group_col`). One VAR fit over the entire input table.
> **Lag order:** Use named param `p` (not `order` — SQL reserved word).
> **Output:** Long format — one row per (variable, horizon step).

### Signature

```sql
ts_forecast_var_by(
    source     VARCHAR,     -- source table name (quoted string)
    date_col   VARCHAR,     -- date column name (quoted string)
    value_cols VARCHAR[],   -- array of value column names
    horizon    INTEGER,     -- periods to forecast
    frequency  VARCHAR,     -- time step between observations
    p          INTEGER,     -- lag order (named param, default: 1)
    params     MAP          -- reserved for future use (default: MAP{})
)
→ TABLE(variable VARCHAR, forecast_step BIGINT, <date_col>, forecast_value DOUBLE)
```

### Examples (verified end-to-end)

```sql
-- VAR(1) — 2-variable system, 14-step ahead, long format output
CREATE OR REPLACE TABLE var_src AS
    SELECT
        (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds,
        (0.6 * SIN(i * 0.4) + 0.1 * COS(i * 0.2)) AS y1,
        (0.05 * SIN(i * 0.4) + 0.7 * COS(i * 0.2)) AS y2
    FROM range(60) t(i);

-- Returns 2 variables * 14 steps = 28 rows in long format
SELECT * REPLACE(ROUND(forecast_value, 6) AS forecast_value)
FROM ts_forecast_var_by('var_src', 'ds', ['y1', 'y2'], 14, '1d')
ORDER BY variable, forecast_step;

-- VAR(2) — higher lag order for longer-range cross-variable dynamics
SELECT * REPLACE(ROUND(forecast_value, 6) AS forecast_value)
FROM ts_forecast_var_by('var_src', 'ds', ['y1', 'y2'], 14, '1d', p:=2)
ORDER BY variable, forecast_step;
```

### Key notes

- **Input format:** Wide — one column per variable, one row per time point. Use standard DuckDB
  pivoting (`PIVOT`) to convert long format to wide before calling `ts_forecast_var_by`.
- **Output format:** Long — `(variable VARCHAR, forecast_step BIGINT, <date_col>, forecast_value DOUBLE)`.
  Use DuckDB `PIVOT` on `variable` to convert back to wide format.
- **Null handling:** Missing values are imputed via linear interpolation before fitting.
  All `value_cols` must have the same number of valid observations after imputation.
- **Minimum obs:** n > k × p + 1 (n = valid obs, k = number of variables, p = lag order).

See [VAR reference](../reference/models/multivariate/var.md) for full docs, pitfalls, and benchmark results.

---

## Explainability — inspecting fit state and decomposing forecasts

Two macros expose the crate's `Inspectable` and `Explainable` surfaces
as wide-STRUCT tables. Both require the `json` extension (auto-loaded
if `autoload_known_extensions` is on).

### `ts_forecast_inspect_by` — fit-state snapshot per group

Returns one row per group with a nullable, uniformly-typed STRUCT
`inspection` that carries the union of every family's payload.
`model_family` names which fields are populated:

| `model_family` | Populated fields |
|----------------|------------------|
| `Ets` | `spec`, `alpha`, `beta`, `gamma`, `phi`, `seasonal_period`, `fitted_values`, `trend_component`, `seasonal_component`, `residuals` |
| `Arima` | `order_p/d/q`, `seasonal_order_P/D/Q/s`, `coefficients`, `aic`, `bic`, `fitted_values`, `residuals` |
| `Theta` | `variant`, `theta`, `alpha`, `seasonal_period`, `fitted_values`, `residuals` |
| `Tbats` | `seasonal_periods`, `box_cox_lambda`, `selected_config`, `aic`, `fitted_values`, `residuals` |
| `Mfles` | `seasonal_period`, `max_rounds`, `multiplicative`, `penalty`, `fitted_values`, `trend_component`, `seasonal_component`, `residuals` |
| `Mstl` | `seasonal_periods`, `iterations`, `fitted_values`, `trend_component`, `seasonal_component`, `residuals` |
| `Laplace` | `leaf_names`, `leaf_weights`, `horizon_dists_json`, `fitted_values`, `residuals` |
| `Regression` | `backend`, `feature_names`, `coefficients`, `intercept`, `coef_std_errors`, `intercept_std_error`, `r_squared`, `fitted_values` |

All other fields are `NULL` for a given family. `raw_json` carries the
untransformed serde payload for callers that need shapes the wide
struct doesn't expose (nested Laplace `GaussianMixture` state, etc.).

**Supported models (fit state):** `AutoETS`, `AutoARIMA`, `AutoTheta`,
`AutoTBATS`, `MFLES`, `AutoMFLES`, `MSTL`, `AutoMSTL`, `Laplace`. Any
other model errors with `does not implement Inspectable`.

```sql
-- ETS: read out per-group smoothing parameters
SELECT product_id,
       (inspection).spec,
       (inspection).alpha,
       (inspection).gamma
FROM ts_forecast_inspect_by('sales', product_id, ds, y, 'AutoETS',
    {seasonal_period: 12});

-- Laplace: see the ensemble the shell picked and each leaf's weight
SELECT product_id,
       list_zip((inspection).leaf_names, (inspection).leaf_weights)
FROM ts_forecast_inspect_by('sales', product_id, ds, y, 'Laplace',
    {seasonal_period: 12, laplace_variant: 'auto_aid'});

-- ARIMA: pull the selected order and BIC
SELECT product_id,
       (inspection).order_p, (inspection).order_d, (inspection).order_q,
       (inspection).bic
FROM ts_forecast_inspect_by('sales', product_id, ds, y, 'AutoARIMA',
    {seasonal_period: 12});
```

### `ts_forecast_explain_by` — per-horizon forecast decomposition

Returns per-group `decomposition` STRUCT with `level` / `trend` /
`seasonal` / `residual` (each a `DOUBLE[]` of length `horizon`; absent
components are `NULL`) plus `named_components_json` for family-specific
extras. `raw_json` carries the untransformed payload.

**Supported models (decomposition):** `ETS` (fixed spec), `MSTL`,
`AutoMSTL`, `Theta`. Other models error with `does not implement Explainable`.

```sql
-- Reconstruct: yhat = level + trend + seasonal + residual
SELECT product_id,
       (decomposition).level,
       (decomposition).trend,
       (decomposition).seasonal
FROM ts_forecast_explain_by('sales', product_id, ds, y, 'ETS', 12,
    {seasonal_period: 12});
```

---

*See also: [Cross-Validation](08-cross-validation.md) | [Evaluation Metrics](09-evaluation-metrics.md)*
