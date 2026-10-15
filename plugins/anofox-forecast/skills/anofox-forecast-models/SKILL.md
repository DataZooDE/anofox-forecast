---
name: anofox-forecast-models
description: >
  Forecasting models and the `ts_forecast_by` / `ts_forecast_var_by` API surface of the
  anofox_forecast DuckDB extension. Covers 36 models (baseline,
  exponential smoothing, state-space ARIMA + Kalman, classical GARCH,
  Theta, multi-seasonal, intermittent-demand, distributional Laplace with
  three variants, panel/global GlobalETS/GlobalTheta/GlobalCroston, and
  multivariate VAR via ts_forecast_var_by), plus exogenous-regressor
  forecasting via the crate's own exog support (ts_forecast_exog_by /
  ts_forecast_exog, model_name = base model + " with regressors"),
  single-series forecasting (ts_forecast), and model
  ensembling (AutoEnsemble via ts_forecast_by, explicit-member
  ts_forecast_ensemble_by, six combination methods, and member/weight
  introspection via ts_ensemble_inspect_by / ts_auto_ensemble_inspect_by),
  parameter surfaces (MAP + STRUCT), model selection guidance, and common
  workflow gotchas. Use when picking a model, building an ensemble, adding
  exogenous regressors, or writing `ts_forecast_by` / `ts_forecast` /
  `ts_forecast_exog_by` / `ts_forecast_agg` / `ts_forecast_var_by` /
  `ts_forecast_ensemble_by` calls.
version: 2026.09.26
user-invocable: false
---

# Anofox Forecast — Models & `ts_forecast_by` Cheat Sheet

**Extension:** `anofox_forecast` (Rust crate `anofox-forecast` v0.16.2) | **DuckDB:** v1.4.5 LTS / v1.5.4+ | **Dual naming:** `ts_*` and `anofox_fcst_ts_*`

36 forecasting models exposed by SQL via three call surfaces (table macro, aggregate, scalar) + `ts_forecast_var_by` for multivariate VAR.

**Prediction intervals** (`yhat_lower`/`yhat_upper`) are each model's own crate-native
interval (corrected formulas since anofox-forecast 0.16.0: Hyndman class-1/2/3 ETS
variances, TBATS innovations state-space variance, GARCH centred on the conditional mean,
etc.) — not a fabricated formula. **R `forecast` parity (FCST-09, Phase 17 plan 17-03):**
of the ten core models, `Naive`/`SeasonalNaive`/`RandomWalkDrift`/`CrostonClassic`,
`SESOptimized`/`ETS 'ANN'` (R `ses()`) and `Theta` are in full R parity; `Holt`/`ETS 'AAN'`,
`HoltWinters`/`ETS 'AAA'`/`ETS 'MAM'` and `AutoETS` are known gaps on AirPassengers with an
identified root cause (mostly R's own optimiser reaching a looser fit, not a defect — see
`validation/reference/README.md`); `AutoARIMA` matches R `auto.arima`'s selected order and
point forecast on every series since 0.16.1 (PR #223), and since 0.16.2 (PR #230/#229) its
95% prediction intervals are full R parity too, including AirPassengers — the prior known
interval gap there is resolved. `'SES'` (fixed `alpha=0.3`) is a different, non-optimised model from
`SESOptimized` — R `ses()` parity is checked against the latter. Some paths return `NULL`
bounds regardless (panel models; `MSTL`/`AutoMSTL` with regressors). See
`docs/api/07-forecasting.md`'s "Prediction intervals, fitted values and input handling"
section for the full per-model table.

## Critical gotchas

1. **Seasonality IS auto-detected when omitted — but explicit is still recommended.** When both `seasonal_period` and `seasonal_periods` are omitted, `ts_forecast_by` / `ts_cv_forecast_by` / the scalar paths / `ts_forecast_agg` all auto-detect the seasonal period internally (`detect_seasonality`) — same rule on every surface (10-10, FCST-06). For models that can run non-seasonally (`ETS`, `AutoETS`, `ARIMA`/`AutoARIMA`, the Theta family, `MFLES`), a period that can't be detected falls back to non-seasonal. For models that **require** a period (`SeasonalNaive`, `SeasonalWindowAverage`, `HoltWinters`, `SeasonalES`, `SeasonalESOptimized`, `MSTL`, `AutoMSTL`, `TBATS`, `AutoTBATS`), a period that can't be detected is a hard error naming the model — never a silent default of 12/2. Run `ts_detect_periods_by` first and pass the result explicitly for control and reproducibility over what silent auto-detection would pick. See `anofox-forecast-detection`.

2. **Model names are case-sensitive.** `'AutoETS'` works, `'autoets'` errors.

3. **`ts_forecast_by` requires frequency as the 7th positional param.** No default:

   ```text
   -- WRONG (missing frequency):
   ts_forecast_by('sales', product_id, ds, y, 'Naive', 12)
   -- CORRECT:
   ts_forecast_by('sales', product_id, ds, y, 'Naive', 12, '1d')
   ```

4. **STRUCT + MAP both work in params.** STRUCT keeps numeric params typed (recommended):

   ```text
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
-- ts_forecast_panel_by needs >= 3 series after alignment -- 'sales' (used elsewhere in
-- this skill) only has 2 product_id groups, so panel examples use their own 3-series fixture.
CREATE OR REPLACE TABLE panel_sales AS
SELECT product_id, (DATE '2024-01-01' + INTERVAL (i) DAY) AS ds,
       20.0 + product_id * 3.0 + 0.6 * i + 5.0 * sin(2 * pi() * i / 7.0) AS y
FROM range(0, 40) t(i) CROSS JOIN (SELECT unnest([1, 2, 3]) AS product_id);

CREATE OR REPLACE TABLE panel_spares AS
SELECT item_id, (DATE '2024-01-01' + INTERVAL (i) DAY) AS ds,
       CASE WHEN (i + item_id) % 4 = 0 THEN 2.0 + (i % 3) ELSE 0.0 END AS qty
FROM range(0, 40) t(i) CROSS JOIN (SELECT unnest([1, 2, 3]) AS item_id);

-- GlobalETS weekly seasonal panel
SELECT * FROM ts_forecast_panel_by('panel_sales', product_id, ds, y, 'GlobalETS', 14, '1d',
    MAP {'seasonal_period': '7'});

-- GlobalTheta trended panel (no config needed)
SELECT * FROM ts_forecast_panel_by('panel_sales', product_id, ds, y, 'GlobalTheta', 14, '1d');

-- GlobalCroston SBA for spare-parts panel
SELECT * FROM ts_forecast_panel_by('panel_spares', item_id, ds, qty, 'GlobalCroston', 6, '1d',
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
CREATE OR REPLACE TABLE var_src AS
    SELECT (DATE '2020-01-01' + INTERVAL (i) DAY) AS ds,
           (0.6 * SIN(i * 0.4) + 0.1 * COS(i * 0.2)) AS y1,
           (0.05 * SIN(i * 0.4) + 0.7 * COS(i * 0.2)) AS y2
    FROM range(60) t(i);

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

## Exogenous regressors — `ts_forecast_exog_by` / `ts_forecast_exog`

Add external drivers (weather, promotions, price) to selected models. Every exog-capable
model routes through the crate's OWN exogenous support (`CalendarAnnotations` +
`predict_with_exog_intervals`) — there is no separate `X`-suffixed model name. `model_name`
reports the base model's own name (including any auto-selected order/spec) with
`" with regressors"` appended once, e.g. `AutoARIMA(3,1,0)(1,0,0)[5] with regressors`,
`ETS(A,A,N) with regressors`.

**Exog-capable models:** `Naive`, `ARIMA`, `AutoARIMA`, `Theta`, `OptimizedTheta`,
`DynamicTheta`, `DynamicOptimizedTheta`, `AutoTheta`, `ETS`, `AutoETS`, `MFLES`, `MSTL`. A
model NOT in this list given a regressor is a hard error naming the model and this list.
`ARIMA` here still requires `arima_order`. A regressor that is **constant throughout
history** (e.g. a holiday flag never observed as 1 during training) is dropped before
fitting — 0 effect by construction, and `model_name` carries NO `" with regressors"` suffix.

**You must supply a FUTURE table** holding the exogenous columns over EXACTLY the
forecast horizon (one future row per horizon step — the model needs the drivers for every
period it predicts, no more, no fewer). `confidence_level` defaults to `0.90` here too.

### `ts_forecast_exog_by` (multi-series — primary surface)

```sql
ts_forecast_exog_by(
    source            VARCHAR,     -- historical table name (quoted string)
    group_col         COLUMN,      -- series identifier (unquoted)
    date_col          COLUMN,      -- date / timestamp (unquoted)
    target_col        COLUMN,      -- value to forecast (unquoted)
    xreg_cols         VARCHAR[],   -- historical exog columns, e.g. ['temp']
    future_source     VARCHAR,     -- future exog table name (quoted string)
    future_date_col   COLUMN,      -- future date column (unquoted)
    future_xreg_cols  VARCHAR[],   -- future exog columns, e.g. ['temp']
    frequency         VARCHAR,     -- '1d', '1mo', ...
    method            VARCHAR,     -- 'AutoARIMA', 'OptimizedTheta', 'MFLES'
    horizon           INTEGER,
    params            MAP or STRUCT
) → TABLE(group_col, forecast_step INT, date TIMESTAMP, yhat DOUBLE, yhat_lower, yhat_upper, model_name)
```

```sql
-- AutoARIMA with a temperature regressor
CREATE TABLE hist AS
SELECT 'A' AS id, DATE '2024-01-01' + INTERVAL (g) DAY AS d,
       10 + g*0.5 + 3*sin(g) AS y, 20.0 + g*0.1 AS temp
FROM range(0,40) t(g);
CREATE TABLE fut AS
SELECT 'A' AS id, DATE '2024-01-01' + INTERVAL (40+g) DAY AS d,
       24.0 + g*0.1 AS temp
FROM range(0,7) t(g);

SELECT * FROM ts_forecast_exog_by('hist', id, d, y,
    ['temp'], 'fut', d, ['temp'], '1d', 'AutoARIMA', 7, MAP{});
-- model_name reports the selected order with ' with regressors' appended, e.g. 'AutoARIMA(3,0,1)(0,1,0)[9] with regressors'
```

### `ts_forecast_exog` (single-series)

Same call without the `group_col` / `frequency` — for one series. Returns
**one row** with array columns `point_forecasts`, `lower_bounds`,
`upper_bounds`, `model_name`, plus `(fcst).aic` / `(fcst).bic` / `(fcst).mse`.

```sql
ts_forecast_exog(source, date_col, target_col, xreg_cols,
                 future_source, future_date_col, future_xreg_cols,
                 method, horizon, params)
    → STRUCT(point_forecasts DOUBLE[], lower_bounds DOUBLE[], upper_bounds DOUBLE[], model_name, ...)
```

```sql
-- Single-series AutoARIMA with regressors (hist/fut without the id column)
SELECT * FROM ts_forecast_exog('hist', d, y,
    ['temp'], 'fut', d, ['temp'], 'AutoARIMA', 7, MAP{});
```

---

## `ts_forecast` (single-series, non-grouped)

The single-series sibling of `ts_forecast_by` — no `group_col`, no
`frequency`. Returns **one row** with array columns.

```sql
ts_forecast(source VARCHAR, date_col COLUMN, target_col COLUMN,
            method VARCHAR, horizon INTEGER, params MAP or STRUCT)
    → STRUCT(point_forecasts DOUBLE[], lower_bounds DOUBLE[], upper_bounds DOUBLE[], model_name, (fcst).aic, (fcst).bic)
```

```sql
CREATE TABLE s AS
SELECT DATE '2024-01-01' + INTERVAL (g) DAY AS d, 10 + g*0.5 + 3*sin(g) AS y
FROM range(0,40) t(g);

SELECT * FROM ts_forecast('s', d, y, 'AutoETS', 7, MAP{});
```

---

## Ensembles (v0.8.0)

Ensembles combine multiple member models per series rather than replacing them — they are a **combination surface over the existing 36 models**, not additional model count. Three entry points plus two introspection functions.

### `ts_forecast_by` with `method='AutoEnsemble'`

Auto-fits `AutoARIMA`, `AutoETS`, and `AutoTheta`; ranks them by in-sample MSE; combines the top-K members.

```sql
ts_forecast_by(
    source VARCHAR, group_col COLUMN, date_col COLUMN, target_col COLUMN,
    'AutoEnsemble', horizon INTEGER, frequency VARCHAR,
    params MAP or STRUCT   -- top_k, combination_method, seasonal_period
) → TABLE(group_col, forecast_step INT, ds, yhat DOUBLE, yhat_lower, yhat_upper, model_name)
```

Params:

| Key | Default | Values |
|---|---|---|
| `top_k` | 3 | Number of top-ranked members to combine |
| `combination_method` | `'mean'` | `'mean'`, `'median'`, `'weighted_mse'`, `'inverse_aic'`, `'stacking'`, `'horizon_adaptive'` |
| `seasonal_period` | 0 | Shared with member models; 0 = non-seasonal |

```sql
-- 60-observation linear series for reliable member convergence (reused by every
-- ensemble/introspection example below)
CREATE OR REPLACE TABLE ae_test AS
SELECT 1 AS id, '2020-01-01'::DATE + INTERVAL (i - 1) DAY AS ds, 10.0 + i * 0.5 AS y
FROM range(1, 61) t(i);

-- AutoEnsemble with default params (top_k=3, combination_method='mean')
-- (sourced from autoensemble.sql Section 2)
SELECT forecast_step, ds, ROUND(yhat, 4) AS yhat, yhat_lower, yhat_upper, model_name
FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 5, '1d')
ORDER BY forecast_step;

-- AutoEnsemble with explicit params (sourced from autoensemble.sql Section 1)
SELECT forecast_step, yhat, model_name
FROM ts_forecast_by(
    'ae_test', id, ds, y,
    'AutoEnsemble', 5, '1d',
    params := {top_k: 3, combination_method: 'mean', seasonal_period: 0}
);
```

**Note:** `yhat_lower` / `yhat_upper` are the crate `AutoEnsemble`'s own `predict_with_intervals` bounds at `confidence_level` (default 0.90) — not NULL. For calibrated (conformal) coverage on top of these bounds, see `anofox-forecast-backtest` and ensemble_intervals.sql.

---

### `ts_forecast_ensemble_by` — explicit member list

User-named member models as a `VARCHAR[]`. Any of the 26-member allowlist (all models except `GARCH`, `AutoEnsemble`) may appear.

```sql
ts_forecast_ensemble_by(
    source VARCHAR,
    group_col COLUMN,
    date_col COLUMN,
    target_col COLUMN,
    members VARCHAR[],        -- e.g. ['AutoARIMA', 'AutoETS', 'Theta']
    horizon INTEGER,
    frequency VARCHAR,
    combination_method := 'mean',   -- named param
    seasonal_period := 0            -- named param
) → TABLE(group_col, forecast_step INT, ds, yhat DOUBLE, yhat_lower DOUBLE, yhat_upper DOUBLE, model_name)
```

```sql
-- Explicit three-member ensemble, mean combination
-- (sourced from ensemble_explicit.sql Section 1)
SELECT * FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    5, '1d',
    combination_method := 'mean',
    seasonal_period := 0
);

-- Seasonal tier-2 members (sourced from ensemble_explicit.sql Section 3 Sample D)
SELECT forecast_step, ROUND(yhat, 4) AS yhat FROM ts_forecast_ensemble_by(
    'ae_test', id, ds, y,
    ['SeasonalNaive', 'HoltWinters'],
    3, '1d',
    combination_method := 'mean', seasonal_period := 12);
```

**Errors:** unknown member name, fewer than 2 members, or blocked model (`GARCH`) each raise a descriptive `InvalidInput` exception naming the offending member.

---

### `ts_ensemble_inspect_by` — inspect explicit-member ensemble

Returns one row per group per member: `member_name`, `weight`, plus group column.

```sql
ts_ensemble_inspect_by(
    source VARCHAR,
    group_col COLUMN,
    date_col COLUMN,
    target_col COLUMN,
    members VARCHAR[],
    combination_method := 'mean',
    seasonal_period := 0
) → TABLE(group_col, member_name VARCHAR, weight DOUBLE)
```

```sql
-- Mean combination: weight == 1/k for every member
-- (sourced from ensemble_inspect.sql Section 1)
SELECT * FROM ts_ensemble_inspect_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    combination_method := 'mean',
    seasonal_period := 0
)
ORDER BY id, member_name;

-- WeightedMSE: inverse-MSE weights; non-negative, sum to 1 per series
-- (sourced from ensemble_inspect.sql Section 2)
SELECT * FROM ts_ensemble_inspect_by(
    'ae_test', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'],
    combination_method := 'weighted_mse',
    seasonal_period := 0
)
ORDER BY id, member_name;
```

---

### `ts_auto_ensemble_inspect_by` — inspect AutoEnsemble members

Returns one row per group per selected member: `member_name`, `weight`, `score` (in-sample MSE), `rank`.

```sql
ts_auto_ensemble_inspect_by(
    source VARCHAR,
    group_col COLUMN,
    date_col COLUMN,
    target_col COLUMN,
    top_k := 3,
    combination_method := 'mean',
    seasonal_period := 0
) → TABLE(group_col, member_name VARCHAR, weight DOUBLE, score DOUBLE, rank BIGINT)
```

```sql
-- AutoEnsemble Mean inspection: weight == 1/k, score > 0, rank in 1..k
-- (sourced from ensemble_inspect.sql Section 3)
SELECT * FROM ts_auto_ensemble_inspect_by(
    'ae_test', id, ds, y,
    top_k := 3,
    combination_method := 'mean',
    seasonal_period := 0
)
ORDER BY id, rank;

-- AutoEnsemble WeightedMSE: weight IS NULL (see gotcha b below), score > 0
-- (sourced from ensemble_inspect.sql Section 4)
SELECT * FROM ts_auto_ensemble_inspect_by(
    'ae_test', id, ds, y,
    top_k := 3,
    combination_method := 'weighted_mse',
    seasonal_period := 0
)
ORDER BY id, rank;
```

---

### Ensemble gotchas

**(a) `ts_cv_forecast_by('AutoEnsemble')` now runs end-to-end (fixed in Phase 10 — the
null-bounds dereference downstream of the CV native's zeroed `ensemble_top_k`/
`ensemble_method` is gone, since every model path is now null-safe).** The manual per-fold
`_ts_forecast_scalar` loop below is kept as a documented alternative (useful when you need a
non-default `top_k`/`combination_method` for the backtest, since `ts_cv_forecast_by` still
can't carry ensemble-specific params through its own `MAP` argument):

```sql
-- Safe conformal CV path for AutoEnsemble (sourced from ensemble_intervals.sql Section 1)
CREATE OR REPLACE TABLE ens_series AS
SELECT CASE WHEN g = 0 THEN 'series_A' ELSE 'series_B' END AS id,
       (DATE '2020-01-01' + INTERVAL (r) DAY) AS ds,
       10.0 + r * 0.5 + sin(r * 0.15) * 2.0 + g * 8.0 AS y
FROM range(120) t(r), range(2) s(g);

-- Step 1: generate folds
CREATE OR REPLACE TABLE ens_folds AS
SELECT * FROM ts_cv_folds_by('ens_series', id, ds, y, 3, 5, MAP{});

-- Step 2: forecast per fold with _ts_forecast_scalar (not ts_cv_forecast_by)
-- Step 3: ts_conformal_calibrate(..., id, ...) -- per-series calibration (Phase 16 D-03)
-- Step 4: ts_forecast_by('AutoEnsemble') + ts_conformal_apply_by(..., id, ds, yhat, 'calib')
-- See examples/forecasting/ensemble_intervals.sql for the full pipeline.
```

**(b) `ts_auto_ensemble_inspect_by` returns NULL `weight` for non-`Mean` `combination_method` (anofox-forecast ≤0.15.10).** The `AutoEnsemble` struct's inner combination weights are stored in a private field not accessible via the public `all_scores()` / `model_count()` API. Mean combination is the only variant for which weights are returned. `score` (in-sample MSE) is always populated. This is a documented upstream-crate limitation.

---

## `ts_forecast_agg` (aggregate)

For custom `GROUP BY` shapes.

```sql
ts_forecast_agg(date_col TIMESTAMP, value_col DOUBLE, method VARCHAR, horizon INTEGER, params MAP)
    → STRUCT(point_forecast DOUBLE[], lower_<pct> DOUBLE[], upper_<pct> DOUBLE[], model_name VARCHAR, insample_fitted DOUBLE[], ...)
```

`<pct>` = `round(100 * confidence_level)` (default `lower_90`/`upper_90`); `confidence_level`
is fixed at bind time and must be a constant expression.

```sql
SELECT product_id, ts_forecast_agg(ds, y, 'AutoETS', 12, MAP{}) AS fcst
FROM sales GROUP BY product_id;
```

Access fields: `(fcst).point_forecast`, `(fcst).lower_90`, etc.

Same params (validated against the same closed key set), same validation, and same SQL-error
behavior for invalid model/parameter values as `ts_forecast_by` (10-10, FCST-06). Input
handling now matches `ts_forecast_by` exactly (Phase 12, D-11): interior `NULL`s are
interpolated, and a `NULL`-valued trailing row keeps the forecast origin anchored to the
series' ORIGINAL last row — only a `NULL` **timestamp** drops a row. Forecast dates come
from the same Rust calendar rule `ts_forecast_by` uses (D-12: month-end-preserving /
day-of-month-clamp stepping), with the frequency auto-detected from the observed dates.

`insample_fitted` comes from each model's own crate `fitted_values()` — no SES(α=0.3)
surrogate for any model. Warmup rows (e.g. ARIMA/SARIMA differencing) are `NULL`; when the
crate's fitted-array length doesn't match the (trimmed) input length, the exported array is
empty rather than mis-aligned. See `docs/api/07-forecasting.md`'s "Prediction intervals,
fitted values and input handling" section for the full input-handling contract.

## Model catalogue (36)

### Automatic selection (6)

| Model | Params | Best for |
|---|---|---|
| `AutoETS` | `seasonal_period`, `model_pool` | Unknown patterns — default pick. A crate fit/predict failure or panic is a hard error naming `AutoETS` — never a silent HoltWinters/Holt/SES substitution. |
| `AutoARIMA` | `seasonal_period` | Unknown patterns, ARIMA family |
| `AutoTheta` | `seasonal_period` | Unknown patterns, Theta family (best RMSE on M5-monthly) |
| `AutoMFLES` | `seasonal_periods[]` | Multiple seasonalities. Currently an alias for `MFLES` (same code path, same reported `model_name` = `MFLES`). |
| `AutoMSTL` | `seasonal_periods[]` | Multiple seasonalities. Currently an alias for `MSTL` (same code path, same reported `model_name` = `MSTL`). |
| `AutoTBATS` | `seasonal_periods[]` | Multiple seasonalities |

`seasonal_period` / `seasonal_periods` are optional everywhere above — omitting them triggers
the auto-detect fallback described in gotcha 1.

### Baseline (7)

| Model | Required | Optional |
|---|---|---|
| `Naive` | — | — |
| `SMA` | — | `window` (default 5) |
| `SeasonalNaive` | `seasonal_period` | — |
| `SeasonalWindowAverage` | `seasonal_period` | — |
| `SES` | — | — (fixed alpha, not SQL-configurable) |
| `SESOptimized` | — | — |
| `RandomWalkDrift` | — | — |

### Exponential smoothing (4)

| Model | Required | Optional |
|---|---|---|
| `Holt` | — | — (fixed smoothing, not SQL-configurable) |
| `HoltWinters` | `seasonal_period` | — (fixed smoothing, not SQL-configurable) |
| `SeasonalES` | `seasonal_period` | — (fixed smoothing, not SQL-configurable) |
| `SeasonalESOptimized` | `seasonal_period` | — |

### Theta (5)

| Model | Optional |
|---|---|
| `Theta` | `seasonal_period` (auto-detected if omitted) — no custom theta coefficient accepted |
| `OptimizedTheta` | `seasonal_period` (auto-detected if omitted) |
| `DynamicTheta` | `seasonal_period` (auto-detected if omitted) — no custom theta coefficient accepted |
| `DynamicOptimizedTheta` | `seasonal_period` (auto-detected if omitted) |
| `AutoTheta` | `seasonal_period` (listed above) |

### Classical volatility (1)

| Model | Required | Optional | Important |
|---|---|---|---|
| `GARCH` | — | `garch_p` (default 1), `garch_q` (default 1) | **`yhat` is VOLATILITY (std-dev = sqrt(variance)), NOT variance.** Use on returns (first differences), not raw price levels. Min obs: p+q+10. |

```sql
CREATE OR REPLACE TABLE returns AS
    SELECT 'Asset_A' AS asset_id, DATE '2024-01-01' + INTERVAL (i) DAY AS ds,
           0.5 * SIN(i * 0.7) + 0.3 * COS(i * 0.3) AS y
    FROM range(40) t(i);

-- GARCH(1,1) — volatility forecast on returns (40 obs minimum > 12)
SELECT asset_id, forecast_step, ds, yhat AS conditional_volatility, model_name
FROM ts_forecast_by('returns', asset_id, ds, y, 'GARCH', 7, '1d');

-- GARCH(1,1) with explicit params
SELECT asset_id, forecast_step, ds, yhat AS conditional_volatility, model_name
FROM ts_forecast_by('returns', asset_id, ds, y, 'GARCH', 7, '1d',
    params := MAP{'garch_p':'1','garch_q':'1'})
```

**Critical:** `yhat` is σ (std-dev), not σ². Square for variance: `yhat * yhat`.
**Critical:** Use on returns (LN differences of prices), not raw levels — non-stationary levels cause α+β→1 divergence.
**Benchmark:** ratio vs arch package = 0.897 on M4 Daily (PASS).

### State-space / ARIMA (3 — `AutoETS`/`AutoARIMA` counted above)

| Model | Required | Optional |
|---|---|---|
| `ETS` | — | `seasonal_period`, `model` (default: runs the crate's own `AutoETS` selection, not a hand-picked `'AAA'`) |
| `ARIMA` | **arima_order** `[p,d,q]` | `seasonal_order` `[P,D,Q]` (+ `seasonal_period`); real crate `ARIMA`/`SARIMA` fit, `model_name` reports the fitted order |
| `Kalman` | — | `kalman_model` (`'local_level'` default \| `'local_linear_trend'`) |

**ETS default:** with no `model` spec, `ETS` runs the crate's own `AutoETS` selection and
reports the chosen spec, e.g. `ETS(A,A,M)` — the same construction standalone `AutoETS`
uses, so `['ETS','ETS']` as an ensemble member equals standalone `'ETS'` bit-for-bit.
**ARIMA:** `arima_order`/`seasonal_order` are the only order keys; `p`/`d`/`q`/`P`/`D`/`Q`/`s`
(bare, unscoped) are still rejected by `ValidateParamKeys`. `'ARIMA'` without `arima_order`
is a hard error naming `AutoARIMA`; with an order, it runs a real crate fit and `model_name`
appends `" with drift"` / `" with non-zero mean"` when the crate's own AICc comparison keeps
a non-zero intercept.

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
| `MFLES` | — | `seasonal_periods[]` (auto-detected single period if omitted) |
| `MSTL` | — | `seasonal_periods[]` (auto-detected single period, or `[12]`, if omitted) |
| `TBATS` | — | `seasonal_periods[]` (auto-detected single period, or `[12]`, if omitted); no Box-Cox toggle accepted |

`iterations` and `stl_method` are rejected by `ValidateParamKeys` — not SQL-configurable.

### Intermittent demand (6)

| Model | Optional |
|---|---|
| `CrostonClassic` | — |
| `CrostonOptimized` | — |
| `CrostonSBA` | — |
| `ADIDA` | — |
| `IMAPA` | — |
| `TSB` | — (fixed smoothing; no custom demand/probability coefficients accepted) |

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
SELECT * FROM ts_forecast_by('sales', product_id, ds, y, 'Laplace', 12, '1mo',
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

-- 1. Detect seasonality (output column is named after the group_col argument, product_id)
CREATE OR REPLACE TABLE periods AS
SELECT product_id, primary_period AS sp
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
