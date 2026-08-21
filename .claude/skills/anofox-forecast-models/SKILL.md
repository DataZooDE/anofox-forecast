---
name: anofox-forecast-models
description: >
  Forecasting models and the `ts_forecast_by` / `ts_forecast_var_by` API surface of the
  anofox_forecast DuckDB extension. Covers 36 models (baseline,
  exponential smoothing, state-space ARIMA + Kalman, classical GARCH,
  Theta, multi-seasonal, intermittent-demand, distributional Laplace with
  three variants, panel/global GlobalETS/GlobalTheta/GlobalCroston, and
  multivariate VAR via ts_forecast_var_by), parameter surfaces (MAP + STRUCT),
  model selection guidance, and common workflow gotchas. Use when picking a
  model or writing `ts_forecast_by` / `ts_forecast_agg` / `ts_forecast_var_by` calls.
version: 0.15.3
user-invocable: false
---

# Anofox Forecast — Models & `ts_forecast_by` Cheat Sheet

**Extension:** `anofox_forecast` v0.15.3 (Rust crate `anofox-forecast` v0.15.3) | **DuckDB:** v1.4.5 LTS / v1.5.4+ | **Dual naming:** `ts_*` and `anofox_fcst_ts_*`

36 forecasting models exposed by SQL via three call surfaces (table macro, aggregate, scalar) + `ts_forecast_var_by` for multivariate VAR.

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

## `ts_forecast_panel_by` (panel / global models)

**Fit-once-emit-many** panel API — pools parameter optimization across all series, then predicts per-series. Use when individual series are short but collectively form a large, homogeneous panel. **Three panel methods:**

```sql
ts_forecast_panel_by(
    source VARCHAR,       -- table name (quoted string)
    group_col COLUMN,     -- series identifier (unquoted)
    date_col COLUMN,      -- date / timestamp (unquoted)
    target_col COLUMN,    -- value to forecast (unquoted)
    method VARCHAR,       -- 'GlobalETS' | 'GlobalTheta' | 'GlobalCroston'
    horizon INTEGER,
    frequency VARCHAR,    -- '1d', '1mo', ...
    params MAP{}          -- optional; see below
) → TABLE(group_col, forecast_step INT, date_col TIMESTAMP, yhat DOUBLE, model_name)
```

### Panel methods

| Method | Best for | Key params |
|---|---|---|
| `'GlobalETS'` | Many related series, shared seasonal dynamics | `seasonal_period` (0=non-seasonal default), `model_pool` ('Reduced' default \| 'Complete') |
| `'GlobalTheta'` | Trended panels, minimal config | none (seasonal_period ignored) |
| `'GlobalCroston'` | Intermittent/spare-parts panels (many zeros) | `croston_variant` ('Classic' default \| 'SBA') |

### Critical panel gotchas

1. **Series dropped below 10 obs:** Short series after alignment emit `DROPPED: too_short` rows — not errors. Check `model_name` in the result.
2. **Minimum 3 series after drop:** Fewer than 3 kept series → `InvalidInputException`. Ensure your panel has enough history.
3. **Point forecasts only (v1):** `yhat_lower`/`yhat_upper` are not populated. Use `ts_conformal_by` separately if intervals needed.
4. **TABLE arg must be a subselect:** The underlying `_ts_forecast_panel_native` uses the subselect pattern internally. Pass only a table name (quoted string) to `ts_forecast_panel_by` — do NOT pass a CTE or subquery as `source`.
5. **GlobalCroston with all-zero panel fails:** Ensure at least one series has ≥ 2 non-zero demand events in the aligned window.
6. **GlobalETS `seasonal_period=0`** → non-seasonal (Reduced pool, `ANN`/`AAdN`/`MNN`/`MAdN` candidates only). Period=1 has the same effect.

### Panel quick examples

```sql
-- GlobalETS weekly seasonal panel
SELECT * FROM ts_forecast_panel_by('sales', product_id, ds, y, 'GlobalETS', 14, '1d',
    MAP {'seasonal_period': '7'});

-- GlobalTheta trended panel (no config needed)
SELECT * FROM ts_forecast_panel_by('sales', product_id, ds, y, 'GlobalTheta', 14, '1d');

-- GlobalCroston SBA for spare-parts panel
SELECT * FROM ts_forecast_panel_by('spares', item_id, ds, qty, 'GlobalCroston', 6, '1d',
    MAP {'croston_variant': 'SBA'});
```

---

## `ts_forecast_var_by` (multivariate VAR)

**Dedicated multivariate function** — distinct from `ts_forecast_by`. Fits a VAR(p) model
across K variables simultaneously (cross-variable dynamics). Returns long format.

**v1 constraints:** Single-panel only (no `group_col`). Named param `p` for lag order (`order` is a SQL reserved word). Point forecasts only (no intervals).

```sql
ts_forecast_var_by(
    source     VARCHAR,     -- source table name (quoted string)
    date_col   VARCHAR,     -- date column name (quoted string)
    value_cols VARCHAR[],   -- array of value column names ['y1', 'y2', ...]
    horizon    INTEGER,     -- periods to forecast
    frequency  VARCHAR,     -- time step between observations
    p          INTEGER,     -- lag order (named param, default: 1)
    params     MAP          -- reserved for future use (default: MAP{})
) → TABLE(variable VARCHAR, forecast_step BIGINT, <date_col>, forecast_value DOUBLE)
```

**Output:** `k_vars × horizon` rows in long format. One row per (variable, forecast_step).

```sql
-- VAR(1) — 2-variable system, 14-step ahead
SELECT * REPLACE(ROUND(forecast_value, 6) AS forecast_value)
FROM ts_forecast_var_by('var_src', 'ds', ['y1', 'y2'], 14, '1d')
ORDER BY variable, forecast_step;
-- Returns 28 rows: y1 × 14 + y2 × 14

-- VAR(2) — higher lag order
SELECT * FROM ts_forecast_var_by('var_src', 'ds', ['y1', 'y2'], 14, '1d', p:=2);
```

**Pitfalls:**
- Use `p:=2` NOT `order:=2` (ORDER is a SQL reserved word).
- All value columns must have the same valid observation count after null imputation.
- Minimum obs: n > k×p+1 (n=obs, k=variables, p=lag order).
- Non-stationary series → unstable coefficient matrix; difference first with `ts_diff_by`.
- **Benchmark:** VAR(1) on synthetic VAR(1) data — MAE ratio vs statsmodels = 1.000 (exact match, PASS).

---

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

## Model catalogue (36)

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

### Classical volatility (1)

| Model | Required | Optional | Important |
|---|---|---|---|
| `GARCH` | — | `garch_p` (default 1), `garch_q` (default 1) | **`yhat` is VOLATILITY (std-dev = sqrt(variance)), NOT variance.** Use on returns (first differences), not raw price levels. Min obs: p+q+10. |

```sql
-- GARCH(1,1) — volatility forecast on returns (40 obs minimum > 12)
SELECT asset_id, forecast_step, ds, yhat AS conditional_volatility, model_name
FROM ts_forecast_by('returns', asset_id, ds, y, 'GARCH', 7, '1d');

-- GARCH(1,1) with explicit params
FROM ts_forecast_by('returns', asset_id, ds, y, 'GARCH', 7, '1d',
    params := MAP{'garch_p':'1','garch_q':'1'})
```

**Critical:** `yhat` is σ (std-dev), not σ². Square for variance: `yhat * yhat`.
**Critical:** Use on returns (LN differences of prices), not raw levels — non-stationary levels cause α+β→1 divergence.
**Benchmark:** ratio vs arch package = 0.897 on M4 Daily (PASS).

### State-space / ARIMA (3 — `AutoETS`/`AutoARIMA` counted above)

| Model | Required | Optional |
|---|---|---|
| `ETS` | — | `seasonal_period`, `model` (`'AAA'`, `'AAN'`, …) |
| `ARIMA` | `p`, `d`, `q` | `P`, `D`, `Q`, `s` |
| `Kalman` | — | `kalman_model` (`'local_level'` default \| `'local_linear_trend'`) |

**Kalman details:**
- `local_level` (default): random walk + noise; h-step forecast is flat at filtered level.
- `local_linear_trend`: level + trend; h-step forecast grows/shrinks linearly.
- Uses fixed variance params (obs_var=1.0, level_var=0.1), NOT MLE-estimated.
- Benchmark: ratio vs statsmodels UnobservedComponents = 1.000 (local_level) / 0.992 (llt), both PASS.

```sql
-- Kalman local_level (default)
SELECT * FROM ts_forecast_by('sales', product_id, ds, y, 'Kalman', 14, '1d');

-- Kalman local_linear_trend
SELECT * FROM ts_forecast_by('sales', product_id, ds, y, 'Kalman', 14, '1d',
    params := MAP{'kalman_model': 'local_linear_trend'});
```

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

## Explainability surfaces (Tier 6)

Two macros expose per-group fit-state and forecast decomposition, each
returning a nullable **wide STRUCT** so the shape is stable across model
families. Both require the `json` extension.

### `ts_forecast_inspect_by(source, group_col, date_col, target_col, method, params?)`

Fit-state snapshot. Signature returns `TABLE(group_col, inspection STRUCT(...))`.
`(inspection).model_family` names which sub-fields are populated:

| `model_family` | Key fields |
|---|---|
| `Ets` | `spec`, `alpha`, `beta`, `gamma`, `phi`, `trend_component`, `seasonal_component`, `residuals` |
| `Arima` | `order_p/d/q`, `seasonal_order_P/D/Q/s`, `coefficients`, `aic`, `bic` |
| `Theta` | `variant`, `theta`, `alpha` |
| `Tbats` | `seasonal_periods`, `box_cox_lambda`, `selected_config`, `aic` |
| `Mfles` | `max_rounds`, `multiplicative`, `penalty`, `trend_component`, `seasonal_component` |
| `Mstl` | `seasonal_periods`, `iterations`, `trend_component`, `seasonal_component` |
| `Laplace` | `leaf_names`, `leaf_weights`, `horizon_dists_json` |

Fields not in the current family are `NULL`. `raw_json` carries the full serde payload.

Supported models: `AutoETS`, `AutoARIMA`, `AutoTheta`, `AutoTBATS`, `MFLES`, `AutoMFLES`, `MSTL`, `AutoMSTL`, `Laplace`. Others raise `does not implement Inspectable`.

```sql
-- Read AutoETS smoothing params per group
SELECT product_id, (inspection).spec, (inspection).alpha
FROM ts_forecast_inspect_by('sales', product_id, ds, y, 'AutoETS',
    {seasonal_period: 12});
```

### `ts_forecast_explain_by(source, group_col, date_col, target_col, method, horizon, params?)`

Per-horizon decomposition. Returns `TABLE(group_col, decomposition STRUCT(horizon, level DOUBLE[], trend DOUBLE[], seasonal DOUBLE[], residual DOUBLE[], named_components_json, raw_json))`. Each array has `horizon` elements; components not produced by a family are `NULL`.

Supported models: `ETS` (fixed spec), `MSTL`, `AutoMSTL`, `Theta`. Others raise `does not implement Explainable`.

```sql
-- Reconstruct: yhat = level + trend + seasonal + residual
SELECT product_id, (decomposition).level, (decomposition).trend, (decomposition).seasonal
FROM ts_forecast_explain_by('sales', product_id, ds, y, 'ETS', 12,
    {seasonal_period: 12});
```

See also: `anofox-forecast-data-prep` (clean input required), `anofox-forecast-detection` (produce `seasonal_period`), `anofox-forecast-backtest` (any model string usable in `ts_cv_forecast_by`), `anofox-forecast-eda` (understand series before picking model).

Reference docs:
- `docs/api/07-forecasting.md` (§ Explainability)
- `docs/reference/models/distributional/laplace.md` (Laplace deep-dive)
- `docs/guides/02-model-selection.md`
