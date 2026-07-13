---
name: anofox-forecast-models
description: >
  Forecasting models and the `ts_forecast_by` API surface of the
  anofox_forecast DuckDB extension. Covers 33 models (baseline,
  exponential smoothing, state-space, ARIMA, Theta, multi-seasonal,
  intermittent-demand, distributional Laplace with three variants),
  parameter surfaces (MAP + STRUCT), model selection guidance, and
  common workflow gotchas. Use when picking a model or writing
  `ts_forecast_by` / `ts_forecast_agg` calls.
version: 0.15.3
user-invocable: false
---

# Anofox Forecast — Models & `ts_forecast_by` Cheat Sheet

**Extension:** `anofox_forecast` v0.15.3 (Rust crate `anofox-forecast` v0.15.3) | **DuckDB:** v1.4.5 LTS / v1.5.4+ | **Dual naming:** `ts_*` and `anofox_fcst_ts_*`

33 forecasting models exposed by SQL via three call surfaces (table macro, aggregate, scalar).

## Critical gotchas

1. **Seasonality is NOT auto-detected.** All models — including the `Auto*` family — treat `seasonal_period` as user-supplied. Run `ts_detect_periods_by` first and pass the result explicitly. See `anofox-forecast-detection`.

2. **Model names are case-sensitive.** `'AutoETS'` works, `'autoets'` errors.

3. **`ts_forecast_by` requires frequency as the 7th positional param.** No default:

   ```sql
   -- WRONG (missing frequency):
   ts_forecast_by('sales', id, ds, y, 'Naive', 12)
   -- CORRECT:
   ts_forecast_by('sales', id, ds, y, 'Naive', 12, '1d')
   ```

4. **STRUCT + MAP both work in params.** STRUCT keeps numeric params typed (recommended):

   ```sql
   -- STRUCT (recommended)
   ts_forecast_by(..., 'HoltWinters', 12, '1d', {seasonal_period: 7})
   -- MAP (all strings, legacy)
   ts_forecast_by(..., 'HoltWinters', 12, '1d', MAP{'seasonal_period': '7'})
   ```

5. **The `_by` output renames the target column to `y`** and adds `forecast_step`, `yhat`, `yhat_lower`, `yhat_upper`, `model_name`.

6. **`ts_forecast_agg` takes `(date, value, model, horizon, params)` directly** — no `LIST(...)` wrapping.

## `ts_forecast_by` (primary surface)

```sql
ts_forecast_by(
    source VARCHAR,           -- table name (quoted string, NOT a CTE)
    group_col COLUMN,         -- series identifier (unquoted)
    date_col COLUMN,          -- date / timestamp (unquoted)
    target_col COLUMN,        -- value to forecast (unquoted)
    method VARCHAR,           -- e.g. 'AutoETS', 'Laplace'
    horizon INTEGER,          -- number of periods ahead
    frequency VARCHAR,        -- '1d', '1mo', '1h', ...
    params MAP or STRUCT      -- model-specific config
) → TABLE(group_col, forecast_step INT, ds, yhat DOUBLE, yhat_lower, yhat_upper, model_name)
```

## `ts_forecast_agg` (aggregate)

For custom `GROUP BY` shapes.

```sql
ts_forecast_agg(date_col TIMESTAMP, value_col DOUBLE, method VARCHAR, horizon INTEGER, params MAP)
    → STRUCT(point_forecast DOUBLE[], lower_90 DOUBLE[], upper_90 DOUBLE[], model_name VARCHAR, insample_fitted DOUBLE[], ...)
```

```sql
SELECT product_id, ts_forecast_agg(ds, y, 'AutoETS', 12, MAP{}) AS fcst
FROM sales GROUP BY product_id;
```

Access fields: `(fcst).point_forecast`, `(fcst).lower_90`, etc.

## Model catalogue (33)

### Automatic selection (6)

| Model | Params | Best for |
|---|---|---|
| `AutoETS` | `seasonal_period`, `model_pool` | Unknown patterns — default pick |
| `AutoARIMA` | `seasonal_period` | Unknown patterns, ARIMA family |
| `AutoTheta` | `seasonal_period` | Unknown patterns, Theta family (best RMSE on M5-monthly) |
| `AutoMFLES` | `seasonal_periods[]` | Multiple seasonalities |
| `AutoMSTL` | `seasonal_periods[]` | Multiple seasonalities |
| `AutoTBATS` | `seasonal_periods[]` | Multiple seasonalities |

### Baseline (6)

| Model | Required | Optional |
|---|---|---|
| `Naive` | — | — |
| `SMA` | — | `window` (default 5) |
| `SeasonalNaive` | `seasonal_period` | — |
| `SES` | — | `alpha` (default 0.3) |
| `SESOptimized` | — | — |
| `RandomWalkDrift` | — | — |

### Exponential smoothing (5)

| Model | Required | Optional |
|---|---|---|
| `Holt` | — | `alpha`, `beta` |
| `HoltWinters` | `seasonal_period` | `alpha`, `beta`, `gamma` |
| `SeasonalES` | `seasonal_period` | `alpha`, `gamma` |
| `SeasonalESOptimized` | `seasonal_period` | — |
| `SeasonalWindowAverage` | `seasonal_period` | — |

### Theta (5)

| Model | Optional |
|---|---|
| `Theta` | `seasonal_period`, `theta` |
| `OptimizedTheta` | `seasonal_period` |
| `DynamicTheta` | `seasonal_period`, `theta` |
| `DynamicOptimizedTheta` | `seasonal_period` |
| `AutoTheta` | `seasonal_period` (listed above) |

### State-space / ARIMA (2 — `AutoETS`/`AutoARIMA` counted above)

| Model | Required | Optional |
|---|---|---|
| `ETS` | — | `seasonal_period`, `model` (`'AAA'`, `'AAN'`, …) |
| `ARIMA` | `p`, `d`, `q` | `P`, `D`, `Q`, `s` |

### Multi-seasonal (3 — `Auto*` counted above)

| Model | Required | Optional |
|---|---|---|
| `MFLES` | `seasonal_periods[]` | `iterations` |
| `MSTL` | `seasonal_periods[]` | `stl_method` |
| `TBATS` | `seasonal_periods[]` | `use_box_cox` |

### Intermittent demand (6)

| Model | Optional |
|---|---|
| `CrostonClassic` | — |
| `CrostonOptimized` | — |
| `CrostonSBA` | — |
| `ADIDA` | — |
| `IMAPA` | — |
| `TSB` | `alpha_d`, `alpha_p` |

### Distributional (1) — Laplace

Streaming likelihood-weighted mixture of leaves (EMA / drift / AR(1) / damped-Holt + optional seasonal / distribution-family leaves). Three zero-config selectors via `laplace_variant`.

| Variant | Best for | Notes |
|---|---|---|
| `auto` (default) | Smooth continuous series | Balanced defaults |
| `auto_aid` | Retail SKU / intermittent counts | AID-based distribution-family leaf selection. Best negative-forecast rate on non-negative counts. |
| `skaters` | Fuller ensemble | Multi-h scoring, stacking, larger leaf set. Slower, more robust. |

Extra param `laplace_seasonal_batch_init: 1` batch-initialises the seasonal-EMA leaf from the last training cycle. **Safe on stationary / declining amplitude; UNSAFE on growing amplitude or phase-shifted seasonality** (collapses to flat). Default off.

```sql
-- Retail-tuned selector on monthly counts
SELECT * FROM ts_forecast_by('sales', id, ds, y, 'Laplace', 12, '1mo',
    {seasonal_period: 12, laplace_variant: 'auto_aid'});
```

`model_name` tags the state: `Laplace(auto,seasonal=7)`, `Laplace(auto_aid,seasonal=12)`, `Laplace(skaters,seasonal=12,batch_init)`.

**Performance context** (M5-monthly, 24 k series × 12-month horizon on 12-core dev box):
- Laplace(auto): MAE 18.99, wall 0.5 s
- AutoTheta: MAE 19.33, wall 0.7 s
- AutoETS: MAE 19.91, wall 15 s (~30× slower)
- SeasonalES (baseline): MAE 26.55

## Parameter surface — universal keys

Every model accepts these via `params`:

| Key | Type | Notes |
|---|---|---|
| `seasonal_period` | INTEGER | e.g. 7 (weekly), 12 (monthly), 24 (hourly-daily), 365 (yearly) |
| `seasonal_periods` | ARRAY (JSON string in MAP form) | Multi-seasonal: `'[24, 168]'` or `[24, 168]` in STRUCT |
| `confidence_level` | DOUBLE | Default 0.90; range (0, 1) |
| `window` | INTEGER | For SMA / SeasonalWindowAverage |
| `model_pool` | VARCHAR | AutoETS: `'complete'` (default), `'reduced'` |
| `laplace_variant` | VARCHAR | `'auto'` / `'auto_aid'` / `'skaters'` — Laplace only |
| `laplace_seasonal_batch_init` | INTEGER (0/1) | Opt-in — Laplace only |

## Frequency strings

| Format | Examples |
|---|---|
| Polars-style | `'1d'`, `'1h'`, `'30m'`, `'1w'`, `'1mo'`, `'1q'`, `'1y'` |
| DuckDB INTERVAL | `'1 day'`, `'1 hour'` |
| Raw integer | `'1'`, `'7'` (days) |

## Model selection — one-liner rules

| Data characteristics | First try | Alternative |
|---|---|---|
| Don't know | `AutoETS` | `Laplace` (auto) |
| Trend, no seasonality | `Holt` | `Theta` |
| Weekly seasonal | `HoltWinters` | `Laplace` (auto) |
| Monthly retail counts | `Laplace` (auto_aid) | `AutoTheta` |
| Multiple seasonalities | `MSTL` | `MFLES` |
| Intermittent (many zeros) | `CrostonSBA` | `Laplace` (auto_aid) |
| Amplitude-declining seasonal | `Laplace` (auto, `batch_init=1`) | `AutoTheta` |
| Speed on large panels | `Laplace` (auto) | `SeasonalES` |
| Short series (< 20 obs) | `Naive` | `SES` |

## Canonical forecast pipeline

```sql
-- 0. Sessions using detection / conformal need json
SET autoinstall_known_extensions = 1;
SET autoload_known_extensions = 1;

-- 1. Detect seasonality
CREATE OR REPLACE TABLE periods AS
SELECT id, primary_period AS sp
FROM ts_detect_periods_by('clean', product_id, ds, y, MAP{});

-- 2. Forecast (panel-modal seasonal_period)
CREATE OR REPLACE TABLE forecasts AS
SELECT * FROM ts_forecast_by('clean', product_id, ds, y,
    'Laplace', 14, '1d',
    {seasonal_period: (SELECT mode() WITHIN GROUP (ORDER BY sp) FROM periods),
     laplace_variant: 'auto'});

-- 3. Inspect
SELECT product_id, forecast_step, ds, yhat, yhat_lower, yhat_upper, model_name
FROM forecasts
ORDER BY product_id, forecast_step;
```

See also: `anofox-forecast-data-prep` (clean input required), `anofox-forecast-detection` (produce `seasonal_period`), `anofox-forecast-backtest` (any model string usable in `ts_cv_forecast_by`), `anofox-forecast-eda` (understand series before picking model).

Reference docs:
- `docs/api/07-forecasting.md`
- `docs/reference/models/distributional/laplace.md` (Laplace deep-dive)
- `docs/guides/02-model-selection.md`
