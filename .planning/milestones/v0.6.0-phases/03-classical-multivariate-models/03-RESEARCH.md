# Phase 3: Classical & Multivariate Models — Research

**Researched:** 2026-08-21
**Domain:** GARCH / Kalman / VAR integration into anofox-forecast DuckDB extension
**Confidence:** HIGH (all claims verified against source files read this session)

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions

**Area 1 — GARCH**
- Exposed via the **existing `ts_forecast_by` surface** as a new `ModelType` arm `method = 'GARCH'` (locked by success criterion 1). Add `GARCH` to the `ModelType` enum + string dispatch in `crates/anofox-fcst-core/src/forecast.rs` and wire it into the unified forecast pipeline.
- **Output is conditional volatility (standard deviation)** = `sqrt(GARCH::forecast_variance(horizon))`. `forecast_value` carries volatility, NOT variance — this MUST be documented explicitly so users aren't misled.
- **Default GARCH(1,1)** (`GARCH::garch_1_1()`); `p` and `q` overridable through the `params` MAP.
- Coefficients (`omega`, `alpha`, `beta`) are **auto-estimated by fit**; optional advanced overrides may be exposed via `params` but are not required.

**Area 2 — Kalman**
- Exposed via **`ts_forecast_by` method = 'Kalman'** (new `ModelType` arm; locked by success criterion 2).
- **Default state-space = local level** (`KalmanForecaster::local_level()`); `local_linear_trend` selectable via `params{'kalman_model': 'local_level' | 'local_linear_trend'}`.
- The `_by` surface **returns h-step forecasts** (consistent with all other `ts_forecast_by` methods). In-sample smoothing exists in the crate but is not what this surface emits.
- Spec selector param key = **`kalman_model`**.

**Area 3 — VAR (multivariate; the design risk)**
- **New dedicated function `ts_forecast_var_by`**, backed by the core **`VAR` struct** (`VAR::fit(&[Vec<f64>])` → `predict(horizon) -> Vec<Vec<f64>>`, K series × horizon) — NOT the single-series `VARForecaster` trait wrapper.
- **Multiple value columns are passed as a `LIST` parameter**: `ts_forecast_var_by(source, group_col?, date_col, value_cols := ['y1','y2','y3'], horizon, frequency, order := 1, ...)`. The list names the K variables.
- **Output is LONG format**: `{variable, forecast_date, forecast_value}` — one row per (variable, horizon step).
- **Lag order via an explicit `order` param** (`VAR::new`/`VARForecaster::new(order)`), default lag 1, overridable.

**Area 4 — Benchmark, Intervals & Docs**
- GARCH & Kalman parity checked against **statsmodels / the `arch` package under `benchmark/.venv`** (R as a fallback reference), **behavioral/approximate** parity.
- **VAR is benchmarked on a synthetic VAR(1) dataset** with known coefficients, compared against statsmodels `VAR`.
- **Point forecasts only for v1**; prediction intervals deferred.
- Docs in **`docs/api/`** and **`docs/reference/models/`**, plus runnable **`examples/*.sql`** snippets verified end-to-end against the built extension.

### Claude's Discretion
- Exact `params` keys for GARCH advanced coefficient overrides and for VAR beyond `order`.
- Whether `ts_forecast_var_by` takes an optional `group_col` (per-panel VAR) or is single-panel for v1.
- The synthetic VAR(1) generator's exact coefficients/size for the benchmark.

### Deferred Ideas (OUT OF SCOPE)
- **Prediction intervals** for GARCH/Kalman/VAR — route through the existing conformal path later, not built into these surfaces in v1.
- **VAR automatic lag-order selection** (AIC/BIC) — explicit `order` param only for v1.
- **Per-panel VAR** (a `group_col` fanning out independent VAR fits) — v1 may be single-panel; revisit if needed.
- GARCH advanced-coefficient user overrides beyond p/q — auto-fit is the v1 default.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| CLAS-01 | User can forecast conditional volatility with GARCH (`ts_forecast_by` method `'GARCH'`) | Upstream API verified: `GARCH::garch_1_1()`, `fit(TimeSeries)`, `forecast_variance(horizon)`. Integration pattern is new ModelType arm in `forecast.rs`. |
| CLAS-02 | User can forecast with a Kalman-filter model (`ts_forecast_by` method `'Kalman'`) | Upstream API verified: `KalmanForecaster::local_level()` and `local_linear_trend()`, both implement `Forecaster` trait — direct drop-in via `extract_forecast`. |
| CLAS-03 | User can produce multivariate forecasts with VAR via `ts_forecast_var_by`, accepting multiple value columns and returning per-variable forecasts | Upstream API verified: `VAR::fit(&[Vec<f64>])`, `predict(horizon) -> Vec<Vec<f64>>`. New FFI export + native table function required; multi-column SQL input via `value_cols VARCHAR[]` Bind param. |
</phase_requirements>

---

## Summary

Phase 3 adds three classical/multivariate model families to the extension. Two (GARCH, Kalman) are straightforward: they extend the existing `ts_forecast_by` dispatch by adding new `ModelType` enum arms in `crates/anofox-fcst-core/src/forecast.rs` and connecting to upstream crate APIs that already implement the `Forecaster` trait or equivalent. The third (VAR) is the design risk: it needs a brand-new FFI export, a new C++ native table function, and a new SQL macro because its I/O shape — K value columns in, K×horizon long-format rows out — cannot be expressed through the existing univariate pipeline.

The critical finding for GARCH and Kalman is that **the `ForecastOptions` struct has no fields for `garch_p`, `garch_q`, or `kalman_model`**. Adding these as separate integer/string fields to the FFI struct is the clean path. The alternative (encoding them in the model string as "GARCH(1,1)") is fragile. The planner must allocate a task for extending `ForecastOptions`/`ForecastOptionsExog` in `types.rs` (Rust) and the corresponding C++ `TsForecastNativeBindData` struct and param-parsing logic.

The critical finding for VAR is that `value_cols` cannot be a DuckDB `LIST` param to a SQL macro in the way you would imagine, because DuckDB SQL macros cannot dynamically project a list of column names from a query_table result. The workable mechanism is: pass `value_cols` as a `VARCHAR[]` literal to `_ts_forecast_var_native`, which reads the column names via Bind-time reflection on the input table's schema and projects them itself. The macro passes the literal list; the C++ Bind reads it from `input.inputs`.

For benchmarks: `statsmodels` 0.14.5 is installed in `benchmark/.venv`; `arch` is **NOT** installed and not in `pyproject.toml`. For GARCH, `statsmodels` does not have a native GARCH implementation — `arch` is the standard Python reference. The planner must choose: (a) add `arch` to `benchmark/pyproject.toml` + reinstall venv, or (b) use the crate's own `generate_var1_data`-style test as the behavioral reference and compare only variance convergence (not parity with a Python reference). VAR: `statsmodels.tsa.api.VAR` IS available and working.

**Primary recommendation:** Split into three plans — (1) GARCH+Kalman using existing `ts_forecast_by` pipeline with `ForecastOptions` struct extension, (2) VAR new multivariate function, (3) benchmark+docs.

---

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| GARCH model fit + variance forecast | Rust core (`crates/anofox-fcst-core`) | FFI boundary | `anofox_forecast::models::garch::GARCH` lives in Rust; only result (sqrt variance) crosses FFI |
| Kalman model fit + h-step forecast | Rust core (`crates/anofox-fcst-core`) | FFI boundary | `KalmanForecaster` implements `Forecaster` trait; integrates same as other trait models |
| VAR multivariate fit + forecast | Rust FFI (`crates/anofox-fcst-ffi`) | Rust core | VAR is called directly from FFI with multi-series input; no existing core wrapper needed |
| Multi-column SQL projection (VAR) | C++ native table function | SQL macro | Bind-time schema reflection extracts value_col indices; macro passes literal column name list |
| Long-format output (VAR) | C++ native table function | — | Same Finalize-emit-rows pattern as panel; emits (variable, forecast_date, forecast_value) |
| GARCH/Kalman params dispatch | C++ Bind (ts_forecast_native.cpp) | Rust FFI | Bind parses `garch_p`, `garch_q`, `kalman_model` from params MAP; passes via extended ForecastOptions struct |
| Benchmark parity | Python (benchmark/.venv) | — | VAR uses `statsmodels.tsa.api.VAR`; GARCH needs `arch` package (not currently installed) |

---

## Standard Stack

### Core (no new deps — everything already in Cargo.toml)

| Library | Source | Purpose | Notes |
|---------|--------|---------|-------|
| `anofox-forecast` 0.15.3 | `Cargo.toml` (pinned) | `GARCH`, `KalmanForecaster`, `VAR` structs | [VERIFIED: /home/simonm/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/garch.rs:1] |
| `anofox-fcst-core` | workspace member | `ModelType` enum, `ForecastOptions`, `forecast()` | [VERIFIED: crates/anofox-fcst-core/src/forecast.rs:93-146] |
| `anofox-fcst-ffi` | workspace member | FFI exports, `ForecastOptions` C struct, new `VARForecastResult` | [VERIFIED: crates/anofox-fcst-ffi/src/types.rs:373-406] |

### Benchmark Python (benchmark/.venv)

| Package | Status | Purpose |
|---------|--------|---------|
| `statsmodels` 0.14.5 | [VERIFIED: installed] | VAR reference (`statsmodels.tsa.api.VAR`), Kalman reference (`statsmodels.tsa.statespace.structural.UnobservedComponents`) |
| `arch` | [VERIFIED: NOT INSTALLED] | GARCH reference — must be added to `benchmark/pyproject.toml` or fallback needed |

### Alternatives Considered

| Instead of | Could Use | Tradeoff |
|------------|-----------|----------|
| `arch` Python package for GARCH benchmark | Manual variance convergence check (compare to unconditional variance) | `arch` is the correct parity reference; skip only if adding dep is blocked |
| Long-format VAR output | Wide format (one col per variable) | Long handles arbitrary K without dynamic schema; consistent with every other surface |
| Encoding GARCH(p,q) in model string | Two new integer fields in `ForecastOptions` | New fields cleaner at FFI boundary; string parsing is fragile and ambiguous |

---

## Package Legitimacy Audit

No new external packages are installed — only stdlib Python packages and existing Cargo dependencies. The only potential addition is `arch` (PyPI) for the GARCH benchmark.

| Package | Registry | Source | Verdict | Disposition |
|---------|----------|--------|---------|-------------|
| `arch` | PyPI | [ASSUMED — not yet verified] | Pending legitimacy check before adding to pyproject.toml | If added, run `pip index versions arch` before committing |

**Recommendation:** Add `arch>=5.3.0` to `benchmark/pyproject.toml` optional/comparison group. Confirm via `pip index versions arch` before merge.

---

## Architecture Patterns

### System Architecture Diagram — Phase 3 Data Flow

```
GARCH/Kalman path (reuses existing pipeline):
  SQL: ts_forecast_by('series', grp, dt, val, 'GARCH', h, freq, MAP{'garch_p':'1','garch_q':'1'})
       └→ _ts_forecast_native(table, h, freq, 'GARCH', params)
           └→ TsForecastNativeBind: parse garch_p/garch_q/kalman_model from params MAP
               └→ TsForecastNativeFinalize: build extended ForecastOptions{garch_p, garch_q, kalman_model}
                   └→ anofox_ts_forecast(values, validity, len, &opts, &result, &error) [FFI]
                       └→ model_str.parse::<ModelType>() → ModelType::GARCH | ModelType::Kalman
                           └→ forecast_garch(values, horizon, p, q) → sqrt(GARCH::forecast_variance(h))
                           └→ forecast_kalman(values, horizon, spec) → KalmanForecaster::predict(h)
                       └→ ForecastOutput{point: volatility_vec, lower:[], upper:[], ...}
                   └→ emit (group, step, date, yhat, model_name) rows

VAR path (new dedicated function):
  SQL: ts_forecast_var_by('sales', date, ['y1','y2','y3'], horizon, freq, order:=1)
       └→ _ts_forecast_var_native(table_with_K_value_cols, h, freq, order, params)
           └→ TsForecastVarNativeBind: read value_cols list, find column indices in schema
               └→ TsForecastVarNativeFinalize: collect K value columns as Vec<Vec<f64>>
                   └→ anofox_ts_forecast_var(flat_matrix, k, n, order, h, &result, &error) [FFI]
                       └→ VAR::new(order).fit(&[series_0..series_K])
                           └→ model.predict(horizon) → Vec<Vec<f64>> (K × horizon)
                   └→ emit (variable_name, forecast_date, forecast_value) LONG rows
```

### Recommended Project Structure for Phase 3

```
crates/anofox-fcst-core/src/
├── forecast.rs          # ADD: ModelType::GARCH, ModelType::Kalman arms; forecast_garch(); forecast_kalman()
crates/anofox-fcst-ffi/src/
├── types.rs             # ADD: garch_p/garch_q/kalman_model fields to ForecastOptions + ForecastOptionsExog
│                        # ADD: VARForecastResult repr(C) struct (k_vars, n_horizon, *mut f64 flat, variable_names?)
├── lib.rs               # ADD: anofox_ts_forecast_var() export; anofox_free_var_forecast_result()
src/
├── include/
│   └── ts_forecast_var_native.hpp    # NEW: forward declaration
├── table_functions/
│   └── ts_forecast_var_native.cpp    # NEW: ~600 lines, mirrors ts_forecast_panel_native.cpp
├── macros/ts_macros.cpp # ADD: ts_forecast_var_by macro entry
├── anofox_forecast_extension.cpp     # ADD: RegisterTsForecastVarNativeFunction call + include
CMakeLists.txt                        # ADD: ts_forecast_var_native.cpp to source list
benchmark/
├── pyproject.toml       # ADD: arch>=5.3.0 (if approved)
├── configs/
│   ├── garch.py         # NEW: GARCH benchmark config
│   ├── kalman.py        # NEW: Kalman benchmark config
│   └── var.py           # NEW: VAR benchmark config (synthetic dataset)
├── m4/
│   ├── garch_benchmark/ # NEW: results dir + run.py
│   ├── kalman_benchmark/# NEW: results dir + run.py
│   └── var_benchmark/   # NEW: results dir + run.py (synthetic, not M4)
examples/forecasting/
└── classical_forecasting_examples.sql  # NEW: GARCH + Kalman + VAR examples
docs/
├── api/07-forecasting.md             # ADD: Classical section (GARCH/Kalman), VAR subsection
├── reference/models/
│   ├── state-space/kalman.md         # NEW
│   └── classical/garch.md            # NEW
│   └── multivariate/var.md           # NEW
```

---

## Critical Finding 1: GARCH Integration

### Upstream API (file-verified)

From `~/.cargo/registry/src/.../anofox-forecast-0.15.3/src/models/garch.rs`:

**Constructor:** [VERIFIED: garch.rs:226] `pub fn garch_1_1() -> Self { Self::new(1, 1) }`

**Fit signature:** [VERIFIED: garch.rs:526-578]
```
impl Forecaster for GARCH {
    fn fit(&mut self, series: &TimeSeries) -> Result<()>
```
Requires `p + q + 10` minimum observations [VERIFIED: garch.rs:531-539]:
```rust
let min_obs = self.p + self.q + 10;
if values.len() < min_obs {
    return Err(ForecastError::InsufficientData { needed: min_obs, got: values.len(), hint: ... })
}
```
So GARCH(1,1) needs **12 observations minimum**.

**Forecast entry point:** [VERIFIED: garch.rs:454-516]
```rust
pub fn forecast_variance(&self, horizon: usize) -> Result<Vec<f64>>
```
Returns `Vec<f64>` of variance values. **`predict()` returns simulated innovations (error × sqrt(σ²)), NOT the variance.** For volatility output, must use `forecast_variance()`, then take `sqrt()` of each element.

**Where sqrt happens:** [VERIFIED: garch.rs:509-511]
```rust
// Return variance forecasts
Ok(sigma2_vals[q..].to_vec())
```
The `forecast_variance` return is pure variance. The `sqrt` to get volatility (std-dev) must be applied in `forecast_garch()` in `anofox-fcst-core/src/forecast.rs`. Example:
```rust
fn forecast_garch(values: &[f64], horizon: usize, p: usize, q: usize) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = GARCH::new(p, q);
    model.fit(&ts).map_err(|e| ForecastError::ComputationError(format!("GARCH fit failed: {}", e)))?;
    let variance = model.forecast_variance(horizon)
        .map_err(|e| ForecastError::ComputationError(format!("GARCH forecast failed: {}", e)))?;
    let volatility: Vec<f64> = variance.iter().map(|&v| v.sqrt()).collect();
    Ok(ForecastOutput {
        point: volatility,
        lower: vec![], upper: vec![], fitted: None, residuals: None,
        model_name: format!("GARCH({},{})", p, q),
        aic: None, bic: None, mse: None,
    })
}
```

**Important:** `GARCH::predict()` returns simulated innovations using hard-coded seed-1 numpy random draws [VERIFIED: garch.rs:594-621]. Do NOT use `Forecaster::predict()` for the forecast surface — use `forecast_variance()` + sqrt.

**Stationarity:** [VERIFIED: garch.rs:276-279] `is_stationary()` checks `sum(alpha) + sum(beta) < 1.0`. MLE optimizer enforces this constraint [VERIFIED: garch.rs:368-371]. Non-convergence is gracefully handled (optimizer keeps initial params).

### ForecastOptions Extension (CRITICAL)

The current `ForecastOptions` struct [VERIFIED: crates/anofox-fcst-ffi/src/types.rs:373-406] has:
- `model: [c_char; 32]` — the method string
- `ets_model: [c_char; 8]` — ETS spec only
- `model_pool: [c_char; 32]`, `laplace_variant: [c_char; 16]`
- No fields for `garch_p`, `garch_q`, `kalman_model`

**Required changes to `types.rs`:** Add to `ForecastOptions` and `ForecastOptionsExog`:
```rust
pub garch_p: c_int,           // GARCH p order (0 = use default 1)
pub garch_q: c_int,           // GARCH q order (0 = use default 1)
pub kalman_model: [c_char; 32], // "local_level" | "local_linear_trend" | "" = default
```

**Required changes to C++ `ts_forecast_native.cpp`:**
- `TsForecastNativeBindData`: add `int64_t garch_p = 0`, `int64_t garch_q = 0`, `string kalman_model = ""`
- `ValidateParamKeys`: add `"garch_p"`, `"garch_q"`, `"kalman_model"` to valid_keys set [VERIFIED: ts_forecast_native.cpp:271-274]
- `TsForecastNativeBind` Bind function: parse these params [VERIFIED: ts_forecast_native.cpp:342-354 pattern]
- `TsForecastNativeFinalize` FFI call site [VERIFIED: ts_forecast_native.cpp:618-651]: populate `opts.garch_p`, `opts.garch_q`, `opts.kalman_model`

**Required changes to `lib.rs` FFI (`anofox_ts_forecast`):** Read `opts.garch_p`, `opts.garch_q`, `opts.kalman_model` from `ForecastOptions` and thread them into `ForecastOptions` core struct (which will need analogous new fields in `anofox-fcst-core/src/forecast.rs`).

The simplest approach for the core: add two optional fields to `ForecastOptions`:
```rust
pub garch_p: usize,       // 0 = use default 1
pub garch_q: usize,       // 0 = use default 1
pub kalman_model: Option<String>,  // None = "local_level"
```
And dispatch in the `forecast()` match:
```rust
ModelType::GARCH => forecast_garch(&clean_values, options.horizon,
    if options.garch_p == 0 { 1 } else { options.garch_p },
    if options.garch_q == 0 { 1 } else { options.garch_q }),
ModelType::Kalman => forecast_kalman(&clean_values, options.horizon,
    options.kalman_model.as_deref()),
```

---

## Critical Finding 2: Kalman Integration

### Upstream API (file-verified)

From `~/.cargo/registry/src/.../anofox-forecast-0.15.3/src/models/kalman_forecaster.rs`:

**KalmanForecaster implements the `Forecaster` trait** [VERIFIED: kalman_forecaster.rs:67]:
```rust
impl Forecaster for KalmanForecaster {
    fn fit(&mut self, series: &TimeSeries) -> Result<()>
    fn predict(&self, horizon: usize) -> Result<Forecast>
```
This means `extract_forecast(&model, horizon, "KalmanForecaster")` works directly — same pattern as every other `Forecaster` model in `forecast.rs`.

**Constructors:** [VERIFIED: kalman_forecaster.rs:35-64]
- `KalmanForecaster::local_level()` — local level (random walk + noise), obs_var=1.0, level_var=0.1
- `KalmanForecaster::local_linear_trend()` — local linear trend, obs_var=1.0, level_var=0.1, trend_var=0.01
- `KalmanForecaster::with_model(StateSpaceModel)` — custom SSM

**Fit path:** [VERIFIED: kalman_forecaster.rs:68-93] Creates `KalmanFilter`, calls `filter(&observations)`, extracts fitted values (predicted_obs[0]) and residuals (innovation[0]).

**Predict path:** [VERIFIED: kalman_forecaster.rs:95-108] Calls `kf.predict(horizon)`, maps `predictions[i][0]` to point. Returns `Forecast::from_values(point)`.

**Integration code (minimal):**
```rust
fn forecast_kalman(values: &[f64], horizon: usize, spec: Option<&str>) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = match spec.unwrap_or("local_level") {
        "local_linear_trend" => KalmanForecaster::local_linear_trend(),
        _ => KalmanForecaster::local_level(),
    };
    model.fit(&ts).map_err(|e| ForecastError::ComputationError(format!("Kalman fit failed: {}", e)))?;
    extract_forecast(&model, horizon, "Kalman")
}
```

**No minimum observation count is documented** in the Kalman source — the filter works on any non-empty series (no explicit `InsufficientData` check in `kalman_forecaster.rs`). Treat as requiring >= 3 (same as other models via the caller's existing check).

**`kalman_model` param:** The CONTEXT specifies param key `kalman_model` with values `"local_level"` (default) or `"local_linear_trend"`. This maps to the two named constructors above.

---

## Critical Finding 3: VAR New Surface — End-to-End Design

### Upstream VAR API (file-verified)

From `~/.cargo/registry/src/.../anofox-forecast-0.15.3/src/models/var.rs`:

**Fit signature:** [VERIFIED: var.rs:96]
```rust
pub fn fit(&mut self, data: &[Vec<f64>]) -> Result<()>
```
`data` is a slice of K vectors, each with N observations. All must be same length. NaN/Inf → `InvalidParameter` error [VERIFIED: var.rs:119-124].

**Predict signature:** [VERIFIED: var.rs:201]
```rust
pub fn predict(&self, horizon: usize) -> Result<Vec<Vec<f64>>>
```
Returns `Vec<Vec<f64>>` of shape `[k][horizon]`. Horizon=0 returns `InvalidParameter` [VERIFIED: var.rs:215-218].

**Minimum observations:** [VERIFIED: var.rs:127-135]: `n > p` required — only needs `p + 1` observations (very low bar). With order=1, needs ≥ 2 observations.

**Error types to handle:** `EmptyData`, `InvalidParameter(String)`, `InsufficientData{needed, got, hint}`, `DimensionMismatch{expected, got}` [VERIFIED: var.rs:99-138].

**Constructor:** [VERIFIED: var.rs:66-76] `VAR::new(order: usize)` — order must be ≥ 1, else `InvalidParameter`.

**The `generate_var1_data` helper in the test module** [VERIFIED: var.rs:439-459] is the exact pattern for the synthetic benchmark generator:
```rust
fn generate_var1_data(n: usize, c: [f64; 2], a: [[f64; 2]; 2], seed: u64) -> Vec<Vec<f64>>
```

### New FFI Export Design

**New struct in `types.rs`:**
```rust
#[repr(C)]
pub struct VARForecastResult {
    /// Flat [k_vars * n_horizon] forecast buffer in variable-major order.
    /// var_forecasts[v * n_horizon + h] = forecast for variable v at step h.
    pub forecasts: *mut c_double,
    pub k_vars: size_t,
    pub n_horizon: size_t,
}

impl Default for VARForecastResult { ... } // null forecasts, 0 dims
```

**New FFI export in `lib.rs`:**
```rust
#[no_mangle]
pub unsafe extern "C" fn anofox_ts_forecast_var(
    flat_data: *const c_double,  // flat [k * n] matrix, variable-major
    k_vars: size_t,              // number of variables K
    series_len: size_t,          // observations per variable N
    order: size_t,               // lag order p (default 1)
    horizon: size_t,             // forecast horizon
    out_result: *mut VARForecastResult,
    out_error: *mut AnofoxError,
) -> bool
```

**Buffer sizing** (apply Phase-2 checked_mul lesson):
```rust
let data_len = k_vars.checked_mul(series_len)
    .ok_or_else(|| "Dimensions overflow")?;
let flat = std::slice::from_raw_parts(flat_data, data_len);
// Reconstruct Vec<Vec<f64>> from flat matrix
let data: Vec<Vec<f64>> = (0..k_vars)
    .map(|v| flat[v * series_len .. (v + 1) * series_len].to_vec())
    .collect();
let mut model = VAR::new(order.max(1));
model.fit(&data)?;
let forecasts = model.predict(horizon)?;
// Write to out_result...
let total = k_vars.checked_mul(horizon).ok_or_else(|| "Output overflow")?;
```

**Free function:**
```rust
#[no_mangle]
pub unsafe extern "C" fn anofox_free_var_forecast_result(result: *mut VARForecastResult)
```

### New C++ Native Table Function: `_ts_forecast_var_native`

**Input schema challenge:** VAR needs K value columns from a single table (not one value_col grouped). The macro passes a subselect projecting exactly the named columns.

**Approach — value_cols as named extra Bind params:**

The macro signature:
```sql
ts_forecast_var_by(source, date_col, value_cols, horizon, frequency, order := 1, params := MAP{})
```
Where `value_cols` is a `VARCHAR[]` literal (e.g., `['y1', 'y2', 'y3']`).

The macro SQL (applying Phase-2 subselect lesson):
```sql
-- ts_forecast_var_by macro body:
SELECT variable, forecast_step, date_col, forecast_value
FROM _ts_forecast_var_native(
    (SELECT date_col, y1, y2, y3 FROM query_table(source::VARCHAR)),
    horizon, frequency, order, value_cols, params
)
```

**Problem:** The macro cannot dynamically build a projection from `value_cols` — DuckDB SQL macros are static templates. The macro would need to be parameterized differently.

**Correct workable mechanism:** The C++ `_ts_forecast_var_native` Bind function receives `value_cols` as a `VARCHAR[]` argument and discovers which columns to read from the input table's schema at Bind time. The macro passes the ENTIRE source row (all columns) via:
```sql
(SELECT * FROM query_table(source::VARCHAR))
```
And passes `value_cols` separately as a positional arg. The Bind function then reads column indices by name.

**Better mechanism (confirmed by precedent):** The `_ts_forecast_var_native` accepts:
1. Input table (via subselect passing all columns: `SELECT date_col, y1, y2, y3 FROM query_table(source::VARCHAR)`)
2. `horizon`, `frequency`, `order` scalar args
3. `value_cols` as a `VARCHAR[]` literal

The macro uses the subselect pattern with the user-supplied column names literal-encoded. Since macros are templates, the COLUMNS are known at call time. The user writes:

```sql
SELECT * FROM ts_forecast_var_by(
    source := 'my_table',
    date_col := 'ds',
    value_cols := ['y1', 'y2', 'y3'],
    horizon := 12,
    frequency := '1d',
    order := 1
)
```

The macro expands to:
```sql
SELECT variable, forecast_step, date_col, forecast_value
FROM _ts_forecast_var_native(
    (SELECT ds, y1, y2, y3 FROM query_table('my_table')),
    12, '1d', 1, ['y1','y2','y3'], MAP{}
)
```

In the C++ Bind, `value_cols` is parsed from `input.inputs[4]` (a LIST Value). The Bind function iterates through `input.input_table_types` to find the index of each named column. The Execute/Finalize reads those column indices from input chunks.

**Output schema:**
```
variable VARCHAR, forecast_step BIGINT, forecast_date TIMESTAMP, forecast_value DOUBLE
```

**v1 decision (Claude's discretion):** No `group_col` for v1. `ts_forecast_var_by` is single-panel — one VAR fit across the entire input table. A per-panel (per-group) VAR is deferred.

**Finalize pattern:** Mirror `ts_forecast_panel_native.cpp` — collect all rows in Execute, fit+predict in Finalize, emit rows. The key difference: instead of collecting one value column per series, collect K value columns; ensure equal length (error if any column has fewer observations than others after NULL dropping).

**VAR NaN handling:** VAR rejects NaN outright [VERIFIED: var.rs:119-124]. The C++ Finalize must pre-impute NaN values or reject series with nulls via an error. Use `fill_nulls_interpolate` before passing to FFI (same as panel), but since all K columns must share the same date range, enforce equal-length alignment: if columns differ in non-null count, surface a clear error ("VAR requires all value columns to have the same number of valid observations").

---

## Critical Finding 4: Params Plumbing — Current State vs What's Needed

### Current ForecastOptions (what exists)
[VERIFIED: crates/anofox-fcst-ffi/src/types.rs:373-406]
```
model: [c_char; 32]
ets_model: [c_char; 8]          ← ETS spec only
horizon: c_int
confidence_level: c_double
seasonal_period: c_int
auto_detect_seasonality: bool
include_fitted: bool
include_residuals: bool
window: c_int
seasonal_periods_str: [c_char; 64]
model_pool: [c_char; 32]        ← AutoETS pool
laplace_variant: [c_char; 16]
laplace_seasonal_batch_init: bool
```

### Fields to Add
```
garch_p: c_int                  ← GARCH order p (0 → default 1)
garch_q: c_int                  ← GARCH order q (0 → default 1)
kalman_model: [c_char; 32]      ← "local_level" | "local_linear_trend" | "" → default
```

### Plumbing Chain

```
params MAP in SQL
  → ParseInt64FromParams(params, "garch_p", 0)   [C++ Bind]
  → TsForecastNativeBindData.garch_p
  → opts.garch_p = (int)bind_data.garch_p        [C++ Finalize, ~line 628-650 pattern]
  → anofox_ts_forecast(..., &opts, ...)           [C++ → Rust FFI call]
  → opts.garch_p as usize                         [Rust FFI, lib.rs ~line 3416-3431 pattern]
  → ForecastOptions.garch_p                       [core ForecastOptions struct]
  → forecast_garch(values, horizon, garch_p, garch_q)  [forecast.rs dispatch]
```

The full params key list after Phase 3 additions:
```
"model", "seasonal_period", "seasonal_periods", "confidence_level", "window",
"model_pool", "laplace_variant", "laplace_seasonal_batch_init",
"garch_p", "garch_q", "kalman_model"
```

---

## Critical Finding 5: Multi-Column Input via DuckDB Table Functions

The VAR function needs K value columns from one source table. There is no existing precedent in the codebase for reading multiple value columns from a table function input.

### How it works in DuckDB

In a table function's `Bind` callback, `input.input_table_types` and `input.input_table_names` give the schema of the input table. The Bind function can discover column indices by name:

```cpp
// In TsForecastVarNativeBind:
auto value_cols_val = input.inputs[4];  // VARCHAR[] literal
auto cols_list = ListValue::GetChildren(value_cols_val);
for (auto &col_val : cols_list) {
    string col_name = col_val.GetValue<string>();
    // find idx in input.input_table_names
    for (idx_t i = 0; i < input.input_table_names.size(); i++) {
        if (input.input_table_names[i] == col_name) {
            bind_data->value_col_indices.push_back(i);
            break;
        }
    }
}
```

Then in Execute, chunk data is read using these indices. The date column is the 0th column (always passed first in the subselect).

### Macro Design

The macro must be a **table macro** (not scalar). The `value_cols` param is a `VARCHAR[]` named parameter with no valid default. The macro's required positional params: `source VARCHAR`, `date_col VARCHAR`, `value_cols VARCHAR[]`, `horizon BIGINT`, `frequency VARCHAR`. Named optional: `order BIGINT := 1`, `params MAP := MAP{}`.

**Critical subselect pattern** [VERIFIED: Phase-2 lesson; ts_macros.cpp:610]:
```cpp
{"ts_forecast_var_by", {"source", "date_col", "value_cols", "horizon", "frequency", nullptr},
 {{"order", "1"}, {"params", "MAP{}"}, {nullptr, nullptr}},
R"(
SELECT variable, forecast_step, date_col, forecast_value
FROM _ts_forecast_var_native(
    (SELECT date_col, * FROM query_table(source::VARCHAR)),
    horizon, frequency, order, value_cols, params
)
)", ...}
```

The `(SELECT date_col, * FROM query_table(...))` passes all columns including the value columns without naming them individually (since the macro doesn't know their names at registration time). The Bind function finds value columns by the `value_cols` name list.

**Alternative:** Pass an explicit column projection by having the user pass `date_col` also in `value_cols` — rejected as confusing. Better: the C++ Bind receives `value_cols` as a `VARCHAR[]` and uses it to select from `*`. The date column is always identified separately.

---

## Critical Finding 6: Benchmark Gaps

### GARCH Benchmark

`arch` Python package is **NOT installed** in `benchmark/.venv` [VERIFIED: runtime import check]. `statsmodels` also does not have GARCH natively [VERIFIED: `from statsmodels.tsa.arch_model import arch_model` → `ModuleNotFoundError`].

**Options:**
1. **Add `arch>=5.3.0` to `benchmark/pyproject.toml`** and run `uv sync` to install. `arch` is a mature, well-maintained package (Kevin Sheppard, >10 years old, widely cited). This is the correct behavioral reference. [ASSUMED: `arch` package legitimacy; confirm via `pip index versions arch` before committing]
2. **Fallback if arch is blocked:** Compare anofox GARCH variance forecasts against the analytical long-run variance (ω/(1-α-β)) and verify stationarity convergence. This is not true parity but is a self-consistency check. Sufficient for the behavioral criterion in D-Area4.

**Recommendation:** Add `arch` to pyproject.toml as an optional comparison dependency (same `comparison` group as `pmdarima`).

### Kalman Benchmark

`statsmodels.tsa.statespace.structural.UnobservedComponents` [VERIFIED: import test passes] provides both local level and local linear trend state-space models. Use:
```python
from statsmodels.tsa.statespace.structural import UnobservedComponents
model_ll = UnobservedComponents(y, 'local level')
result_ll = model_ll.fit(disp=False)
forecast_ll = result_ll.forecast(horizon)
```
This is a valid behavioral reference. Approximate parity (not exact numeric match) is expected because `KalmanForecaster::local_level()` uses default variance params (obs_var=1.0, level_var=0.1) while statsmodels estimates them via MLE.

### VAR Benchmark

`statsmodels.tsa.api.VAR` is installed and working [VERIFIED: import + smoke test pass]. Use:
```python
from statsmodels.tsa.api import VAR
data = np.array([y1, y2]).T
model = VAR(data)
result = model.fit(maxlags=1, ic=None)
forecast = result.forecast(data[-1:], steps=horizon)
```
Use `generate_var1_data` pattern [VERIFIED: var.rs:439-459] with known coefficients (e.g., `c=[0.5, 0.3]`, `a=[[0.6, 0.1],[0.05, 0.7]]`, N=200, seed=42). Parity criterion: coefficient recovery within 5% of known ground truth; forecast MAE close to statsmodels reference on same data.

### Benchmark Structure

Mirror Phase 2 benchmark/m4/global_benchmark/ pattern:
```
benchmark/m4/garch_benchmark/
  run.py              # venv run: benchmark/.venv/bin/python run.py --run
  results/            # committed .parquet files
benchmark/m4/kalman_benchmark/
  run.py
  results/
benchmark/m4/var_benchmark/   # uses synthetic data, not M4
  run.py
  results/
```
All scripts use `benchmark/.venv/bin/python`, never `python3`. For VAR the source is synthetic (not M4 dataset), so no `datasetsforecast` M4 download needed.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| GARCH MLE estimation | Custom optimizer | `GARCH::optimize_parameters()` (upstream) | Upstream uses Nelder-Mead with 7 restart points and statsforecast-compatible sigma² formula |
| GARCH variance computation | Manual σ² recursion | `GARCH::forecast_variance(horizon)` | Upstream matches statsforecast's `garch_sigma2` exactly (flipped alpha/beta, NaN init) |
| Kalman filter recursion | Custom state-space | `KalmanForecaster::local_level()` / `local_linear_trend()` | Upstream has full filter+predict implemented |
| VAR OLS estimation | Custom least-squares | `VAR::fit()` (upstream, uses `ols_fit` helper) | Equation-by-equation OLS with regressor map already implemented |
| Multi-step VAR forecast | Custom rolling | `VAR::predict(horizon)` | Uses rolling history buffer correctly |
| GARCH stationarity enforcement | Post-hoc clipping | Upstream MLE enforces α+β<1 constraint | Optimizer constraint [VERIFIED: garch.rs:368-371] |

---

## Common Pitfalls

### Pitfall 1: GARCH `predict()` vs `forecast_variance()`
**What goes wrong:** Using `Forecaster::predict()` instead of `forecast_variance()` for the volatility surface. `predict()` returns simulated innovations (error × sqrt(σ²)) using pre-seeded random draws — the values are noisy and seed-dependent, not the analytical variance forecast.
**How to avoid:** Explicitly call `forecast_variance(horizon)` then `sqrt()` each element. Do not go through the `extract_forecast(&model, horizon, name)` helper which calls `model.predict()`.
**Warning signs:** Forecast values oscillate randomly rather than converging to unconditional variance.

### Pitfall 2: `ForecastOptions` Struct ABI Change
**What goes wrong:** Adding fields to `ForecastOptions` in `types.rs` without updating the C++ `ForecastOptions` struct in `anofox_fcst_ffi.h` (which is auto-generated by cbindgen). If cbindgen is not re-run after the Rust change, the C++ struct has the old layout and field accesses will be misaligned.
**How to avoid:** Run `make header` (or the cbindgen command) after modifying `types.rs`. Check that `anofox_fcst_ffi.h` in `src/include/` shows the new fields before compiling C++.
**Warning signs:** Segfault or wrong values in `opts.garch_p` when read in C++ after the FFI call.

### Pitfall 3: VAR NaN/Inf Rejection
**What goes wrong:** Passing a value column with NaN or Inf values to `VAR::fit()`. The upstream rejects it immediately [VERIFIED: var.rs:119-124] with `InvalidParameter("Variable {} contains NaN or Inf values")`.
**How to avoid:** Apply `fill_nulls_interpolate` to each value column in the C++ Finalize before building the flat matrix for the FFI call. If a column has leading/trailing nulls that cannot be interpolated, skip with an error row in output.

### Pitfall 4: VAR Equal-Length Requirement
**What goes wrong:** Two value columns have different numbers of non-null observations. `VAR::fit()` requires [VERIFIED: var.rs:112-118] `DimensionMismatch` if series lengths differ.
**How to avoid:** After null imputation, verify all K columns have the same effective length. If not, emit an error (or truncate to the shortest — document the choice clearly in user-facing error message).

### Pitfall 5: VAR Order vs Series Length
**What goes wrong:** User passes `order=5` for a 6-observation series. `VAR(5)` needs n > p, so n ≥ 6 — minimum `n_eff = n - p = 1` which is mathematically valid but practically useless. With k variables, the OLS system has `k * k * p + k` params, which can exceed n_eff easily.
**How to avoid:** Add a soft check in C++ Bind or Finalize: warn/error when `n_eff < k * p + 1` (OLS underdetermined). The upstream `ols_fit` may return NaN coefficients in this case.

### Pitfall 6: Subselect Macro Pattern for VAR
**What goes wrong:** Passing `query_table(source::VARCHAR)` directly as a TABLE argument to `_ts_forecast_var_native` without the subselect wrapper [VERIFIED: Phase-2 lesson from 02-1-SUMMARY.md:155-160]. The macro silently fails to register (0 rows in `duckdb_functions()`).
**How to avoid:** Always use `(SELECT date_col, * FROM query_table(source::VARCHAR))` subselect pattern. Confirmed this is the established convention [VERIFIED: ts_macros.cpp:610].

### Pitfall 7: `arch` Package Missing for GARCH Benchmark
**What goes wrong:** Benchmark script imports `arch` and crashes. `arch` is not in `benchmark/pyproject.toml` and not installed in `.venv`.
**How to avoid:** Either add `arch` to pyproject.toml optional `comparison` group + `uv sync`, OR implement the fallback variance-convergence check without Python parity.

### Pitfall 8: GARCH Minimum Observations
**What goes wrong:** Short series (< 12 obs for GARCH(1,1)) causes `InsufficientData` error which surfaces as group-skip (continue) in C++ Finalize, silently emitting no forecast rows for that group.
**How to avoid:** Document in SQL function that GARCH(p,q) requires `p + q + 10` minimum observations. Surface a clear error or DROPPED row (not silent skip) so users know why a group is missing from output.

### Pitfall 9: GARCH Non-Stationarity Warning
**What goes wrong:** On non-returns data (e.g., raw price levels), the MLE optimizer may produce nearly non-stationary parameters (α+β → 1). The forecast variance diverges to infinity at long horizons.
**How to avoid:** After fitting, check `model.is_stationary()` [VERIFIED: garch.rs:276-279]. If false, the output variance forecasts may be unreliable — document that GARCH is designed for returns (first differences of prices), not raw levels.

---

## Code Examples

### Pattern 1: New ModelType Arm in forecast.rs

```rust
// Source: crates/anofox-fcst-core/src/forecast.rs (new code, modeled on existing arms)
// In the ModelType enum (add after Laplace):
ModelType::GARCH,
ModelType::Kalman,

// In FromStr match (exact strings):
"GARCH" => return Ok(ModelType::GARCH),
"Kalman" | "Kalman" => return Ok(ModelType::Kalman),

// In ForecastOptions (new fields):
pub garch_p: usize,           // 0 = default 1
pub garch_q: usize,           // 0 = default 1
pub kalman_model: Option<String>,  // None = "local_level"

// In forecast() match dispatch:
ModelType::GARCH => forecast_garch(
    &clean_values,
    options.horizon,
    if options.garch_p == 0 { 1 } else { options.garch_p },
    if options.garch_q == 0 { 1 } else { options.garch_q },
),
ModelType::Kalman => forecast_kalman(
    &clean_values,
    options.horizon,
    options.kalman_model.as_deref(),
),
```

### Pattern 2: GARCH forecast function

```rust
// Source: crates/anofox-fcst-core/src/forecast.rs (new function)
use anofox_forecast::models::garch::GARCH;

fn forecast_garch(values: &[f64], horizon: usize, p: usize, q: usize) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = GARCH::new(p, q);
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("GARCH fit failed: {}", e)))?;
    // Use forecast_variance(), NOT predict() — predict() returns simulated innovations
    let variance = model
        .forecast_variance(horizon)
        .map_err(|e| ForecastError::ComputationError(format!("GARCH forecast failed: {}", e)))?;
    // Output is volatility (std-dev), not variance — sqrt each element
    let volatility: Vec<f64> = variance.iter().map(|&v| v.sqrt()).collect();
    Ok(ForecastOutput {
        point: volatility,
        lower: vec![],
        upper: vec![],
        fitted: None,
        residuals: None,
        model_name: format!("GARCH({},{})", p, q),
        aic: None,
        bic: None,
        mse: None,
    })
}
```

### Pattern 3: Kalman forecast function

```rust
// Source: crates/anofox-fcst-core/src/forecast.rs (new function)
use anofox_forecast::models::kalman_forecaster::KalmanForecaster;

fn forecast_kalman(values: &[f64], horizon: usize, spec: Option<&str>) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = match spec.unwrap_or("local_level") {
        "local_linear_trend" => KalmanForecaster::local_linear_trend(),
        _ => KalmanForecaster::local_level(),
    };
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("Kalman fit failed: {}", e)))?;
    // KalmanForecaster implements Forecaster — extract_forecast works directly
    extract_forecast(&model, horizon, "Kalman")
}
```

### Pattern 4: VAR FFI inner logic (testable)

```rust
// Source: crates/anofox-fcst-ffi/src/lib.rs (new inner function, mirrors forecast_panel_impl)
use anofox_forecast::models::var::VAR;

pub(crate) fn forecast_var_impl(
    flat: &[f64],
    k_vars: usize,
    series_len: usize,
    order: usize,
    horizon: usize,
) -> Result<Vec<Vec<f64>>, anofox_fcst_core::ForecastError> {
    if k_vars == 0 || series_len == 0 {
        return Err(anofox_fcst_core::ForecastError::InvalidInput("Empty data".into()));
    }
    // Reconstruct K series from flat matrix
    let data: Vec<Vec<f64>> = (0..k_vars)
        .map(|v| flat[v * series_len..(v + 1) * series_len].to_vec())
        .collect();
    let mut model = VAR::new(order.max(1));
    model.fit(&data)
        .map_err(|e| anofox_fcst_core::ForecastError::ComputationError(format!("{}", e)))?;
    model.predict(horizon)
        .map_err(|e| anofox_fcst_core::ForecastError::ComputationError(format!("{}", e)))
}
```

### Pattern 5: ts_forecast_var_by macro entry (modeled on ts_forecast_panel_by)

```cpp
// Source: src/macros/ts_macros.cpp — append after ts_forecast_panel_by entry
// Apply Phase-2 subselect lesson: never pass query_table() directly as TABLE arg
{"ts_forecast_var_by",
 {"source", "date_col", "value_cols", "horizon", "frequency", nullptr},
 {{"order", "1"}, {"params", "MAP{}"}, {nullptr, nullptr}},
R"(
SELECT variable, forecast_step, date_col, forecast_value
FROM _ts_forecast_var_native(
    (SELECT date_col, * FROM query_table(source::VARCHAR)),
    horizon,
    frequency,
    order,
    value_cols,
    params
)
)",
"VAR multivariate forecasting. Returns one row per (variable, horizon step) in long format. "
"value_cols is a VARCHAR[] of column names from source. "
"order is the lag order p (default 1).",
"SELECT * FROM ts_forecast_var_by('returns', 'ds', ['equity','bond','fx'], 12, '1d', order:=2)",
"forecasting"},
```

### Pattern 6: Synthetic VAR benchmark comparison

```python
# Source: benchmark/m4/var_benchmark/run.py (new)
# Run under: benchmark/.venv/bin/python run.py --run
import numpy as np
from statsmodels.tsa.api import VAR

def generate_var1_data(n=200, c=(0.5, 0.3), a=((0.6, 0.1), (0.05, 0.7)), seed=42):
    rng = np.random.default_rng(seed)
    y = np.zeros((n, 2))
    y[0] = rng.uniform(-1, 1, 2)
    for t in range(1, n):
        noise = rng.uniform(-0.01, 0.01, 2)
        y[t, 0] = c[0] + a[0][0]*y[t-1, 0] + a[0][1]*y[t-1, 1] + noise[0]
        y[t, 1] = c[1] + a[1][0]*y[t-1, 0] + a[1][1]*y[t-1, 1] + noise[1]
    return y

# Reference: statsmodels VAR
data = generate_var1_data()
sm_model = VAR(data)
sm_result = sm_model.fit(maxlags=1, ic=None)
sm_forecast = sm_result.forecast(data[-1:], steps=14)

# Compare with anofox ts_forecast_var_by output via CLI subprocess
# (same pattern as panel benchmark — use build/release/duckdb -unsigned)
```

---

## Docs Layout

### Reference to Phase 2 pattern (no global model docs in `docs/reference/models/`)

The Phase 2 docs added a Panel section to `docs/api/07-forecasting.md` [VERIFIED: grep on 07-forecasting.md line 318]. No new subdirectory was created under `docs/reference/models/` for global models — but the sub-dirs `baseline`, `distributional`, `exponential-smoothing`, `intermittent`, `multi-seasonal`, `state-space`, `theta` already exist [VERIFIED: ls output].

### Phase 3 docs targets

**New directories:**
- `docs/reference/models/classical/` → `garch.md`
- `docs/reference/models/multivariate/` → `var.md`
- Kalman goes in `docs/reference/models/state-space/kalman.md` (already a state-space dir)

**Existing file to extend:**
- `docs/api/07-forecasting.md`: Add "Classical Models" subsection after the Panel section (GARCH, Kalman). Add "Multivariate" subsection for VAR.

**Critical doc requirement (PR #230 rule):** Every SQL example in docs must be verified end-to-end against the built extension before merge. No eyeballing.

---

## State of the Art / Existing Art

| Area | Notes |
|------|-------|
| GARCH | GARCH(1,1) is the de facto standard for financial volatility — nearly always better than higher orders. Upstream already implements statsforecast-matching MLE (Nelder-Mead, multiple restarts). |
| Kalman | `KalmanForecaster::local_level()` defaults (obs_var=1.0, level_var=0.1) produce reasonable forecasts but are not MLE-estimated — this is a simplification vs statsmodels. |
| VAR | OLS equation-by-equation estimation (upstream). Not full MLE; no automatic lag selection in v1. Correct behavior for the stated scope. |

---

## Recommended Plan Split

Given three distinct model families with different integration complexity:

**Plan 03-1: GARCH + Kalman (ts_forecast_by extension)**
- Extend `ForecastOptions` struct (types.rs, Rust + C++ header via cbindgen)
- Add `TsForecastNativeBindData` fields + ValidateParamKeys + Bind parsing (C++)
- Add `ModelType::GARCH` + `ModelType::Kalman` to enum + FromStr + name() (Rust core)
- Implement `forecast_garch()` + `forecast_kalman()` (Rust core)
- Wire FFI `anofox_ts_forecast`: read new opts fields, pass to core (Rust FFI lib.rs)
- Unit tests for both models in FFI test module
- Examples: `examples/forecasting/classical_forecasting_examples.sql` (GARCH + Kalman sections)
- **Tracer approach:** verify GARCH working first (simpler output: single Vec<f64>), then Kalman

**Plan 03-2: VAR multivariate function**
- New `VARForecastResult` repr(C) struct (types.rs)
- New `anofox_ts_forecast_var` FFI export + `anofox_free_var_forecast_result` + `forecast_var_impl` inner fn + unit tests (lib.rs)
- New `cbindgen.toml` update: add `VARForecastResult` to include list
- New `src/include/ts_forecast_var_native.hpp` forward declaration
- New `src/table_functions/ts_forecast_var_native.cpp` (~600 lines, mirror panel)
- New `ts_forecast_var_by` macro entry (ts_macros.cpp)
- Registration in `anofox_forecast_extension.cpp` + `CMakeLists.txt`
- Examples: VAR section in `classical_forecasting_examples.sql`

**Plan 03-3: Benchmarks + docs**
- Add `arch` to `benchmark/pyproject.toml` (or fallback)
- GARCH benchmark: `benchmark/m4/garch_benchmark/` (M4 Daily or synthetic returns)
- Kalman benchmark: `benchmark/m4/kalman_benchmark/` (statsmodels UnobservedComponents)
- VAR benchmark: `benchmark/m4/var_benchmark/` (synthetic VAR(1), statsmodels reference)
- Docs: `docs/api/07-forecasting.md` Classical + Multivariate sections
- Docs: `docs/reference/models/classical/garch.md`, `state-space/kalman.md`, `multivariate/var.md`

---

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `arch` PyPI package is legitimate, well-maintained (Kevin Sheppard), safe to add as a benchmark dependency | Benchmark Gaps | Low: arch is the standard Python ARCH/GARCH library, widely cited in academia. Verify via `pip index versions arch` before adding. |
| A2 | `(SELECT date_col, * FROM query_table(source::VARCHAR))` passes all columns including value columns to the C++ Bind, allowing runtime name lookup | VAR Macro Design | Medium: DuckDB `*` in a subselect in a macro should work but is untested for this pattern. Alternative: require user to explicitly name all cols in a separate LIST param. |
| A3 | `KalmanForecaster::local_level()` will produce approximately parity-level results vs `statsmodels.tsa.statespace.structural.UnobservedComponents` | Kalman Benchmark | Low: different implementations will have quantitatively different results; behavioral criterion only requires similar direction/magnitude, not exact match. |
| A4 | `cbindgen.toml` adding `VARForecastResult` to export include list is the correct way to expose the new struct | VAR FFI | Low: this is exactly the Phase-2 pattern for `PanelForecastResult` [VERIFIED: 02-1-SUMMARY.md:46] |

---

## Open Questions

1. **GARCH on non-returns data (GARCH stationarity)**
   - What we know: GARCH is designed for financial returns (zero-mean, volatility clustering). On raw price levels, the mean term is subtracted before computing residuals [VERIFIED: garch.rs:545-548], but if the series has strong trend the MLE may produce barely-stationary params.
   - What's unclear: Should the C++ layer warn when GARCH is applied to non-stationary series (ADF p > 0.05)?
   - Recommendation: Expose `is_stationary()` as part of the model_name metadata (e.g., model_name = "GARCH(1,1)[non-stationary]") rather than rejecting — let the user decide.

2. **v1 group_col support for VAR**
   - What we know: Deferred in CONTEXT.md. Single-panel for v1.
   - What's unclear: Whether the C++ Bind should accept and silently ignore a `group_col` param, or strictly error on it.
   - Recommendation: No `group_col` in v1. Document it explicitly as "single-panel only" in the function signature + docs.

3. **`arch` package approval**
   - What we know: Not in pyproject.toml; not installed; required for GARCH parity benchmark.
   - Recommendation: Add to optional `comparison` group in pyproject.toml and run `uv sync --extra comparison`. If blocked, use variance-convergence self-check as fallback.

---

## Environment Availability

| Dependency | Required By | Available | Version | Fallback |
|------------|------------|-----------|---------|----------|
| statsmodels | Kalman + VAR benchmark | ✓ | 0.14.5 | — |
| arch (Python) | GARCH benchmark | ✗ | — | Variance-convergence check (no external parity) |
| statsmodels.tsa.api.VAR | VAR benchmark | ✓ | same | — |
| statsmodels.tsa.statespace.structural.UnobservedComponents | Kalman benchmark | ✓ | same | — |
| anofox-forecast 0.15.3 | All models | ✓ | 0.15.3 | — |
| DuckDB build CLI | Benchmarks | ✓ | build/release/duckdb | — |

**Missing with no fallback:** None (arch has a fallback).

**Missing requiring action:** `arch` — either add to pyproject.toml or use fallback for GARCH benchmark.

---

## Security Domain

> `security_enforcement: true` per config.json; `nyquist_validation: false` per config.

| ASVS Category | Applies | Control |
|---------------|---------|---------|
| V5 Input Validation | Yes | FFI: null-pointer checks on all ptr args (established pattern); VAR: NaN/Inf rejection in upstream (re-validate before FFI call); GARCH: min-obs check; Kalman: non-empty series check |
| V6 Cryptography | No | No crypto operations |
| V2 Authentication | No | Extension context — DuckDB handles auth |

**Threat: FFI buffer overflow via VAR flat matrix.** Mitigated by `checked_mul(k_vars, series_len)` before `slice::from_raw_parts` — same pattern as Phase-2 `anofox_ts_forecast_panel` [VERIFIED: lib.rs:7013-7016].

---

## Sources

### Primary (HIGH confidence — file-verified this session)

- `~/.cargo/registry/src/.../anofox-forecast-0.15.3/src/models/garch.rs` — full GARCH API, fit signature, forecast_variance, is_stationary, minimum observations
- `~/.cargo/registry/src/.../anofox-forecast-0.15.3/src/models/kalman_forecaster.rs` — KalmanForecaster API, Forecaster impl, local_level/local_linear_trend constructors
- `~/.cargo/registry/src/.../anofox-forecast-0.15.3/src/models/var.rs` — VAR::fit signature, predict return type, error types, minimum obs, generate_var1_data pattern
- `crates/anofox-fcst-core/src/forecast.rs` — ModelType enum (lines 93-146), ForecastOptions struct (lines 310-348), forecast() dispatch (lines 570-681), forecast_with_model() pattern (lines 916-1022), make_timeseries/extract_forecast helpers
- `crates/anofox-fcst-ffi/src/types.rs` — ForecastOptions C struct (lines 373-406), PanelForecastResult (lines 415-425)
- `crates/anofox-fcst-ffi/src/lib.rs` — anofox_ts_forecast_panel pattern (lines 6860-7059), catch_unwind pattern, checked_mul
- `src/table_functions/ts_forecast_native.cpp` — ValidateParamKeys (lines 270-306), Bind (lines 312-370), ForecastOptions population (lines 617-651)
- `src/macros/ts_macros.cpp` — ts_forecast_panel_by macro (lines 596-623), subselect pattern
- `benchmark/pyproject.toml` — package list; arch not present
- `benchmark/.venv` — runtime import tests: statsmodels 0.14.5 ✓, arch ✗

### Secondary (MEDIUM confidence — Phase 2 summaries)
- `.planning/phases/02-global-panel-models/02-1-SUMMARY.md` — subselect TABLE arg lesson, PanelForecastError wrapper pattern
- `.planning/phases/02-global-panel-models/02-3-SUMMARY.md` — CLI subprocess benchmark pattern, per-series date alignment

---

## Metadata

**Confidence breakdown:**
- GARCH/Kalman integration: HIGH — upstream API fully read; plumbing chain traced end-to-end
- VAR multi-column mechanism: MEDIUM-HIGH — mechanism designed from first principles; one ASSUMED about `SELECT *` in macro (A2)
- Benchmark gaps: HIGH — runtime import tests confirm arch missing, statsmodels present
- Docs layout: HIGH — actual directory listing verified

**Research date:** 2026-08-21
**Valid until:** 2026-09-20 (stable codebase; anofox-forecast 0.15.3 is pinned)
