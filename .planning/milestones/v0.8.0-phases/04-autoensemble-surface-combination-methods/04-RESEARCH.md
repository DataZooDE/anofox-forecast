# Phase 4: AutoEnsemble Surface + Combination Methods — Research

**Researched:** 2026-08-30
**Domain:** Ensemble forecasting — crate wiring through FFI → C++ → SQL macro
**Confidence:** HIGH — all findings verified by reading source files in this session

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **Delivery vehicle:** `method := 'AutoEnsemble'` on the existing `ts_forecast_by` — method-string dispatch, same vehicle as GARCH/Kalman in v0.7.0.
- **Parameter transport:** extend `ForecastOptions` FFI struct additively with ensemble fields (mirrors `garch_p`/`garch_q`/`kalman_model`). No parallel options struct; preserve ABI back-compat.
- **`combination_method` representation:** case-insensitive string — `'mean'`, `'median'`, `'weighted_mse'`, `'inverse_aic'`, `'stacking'`, `'horizon_adaptive'` (accept common spellings). Maps to crate `CombinationMethod` enum.
- **Default `combination_method`:** **Mean** (overrides crate's `WeightedMSE` default). Rationale: makes internal-consistency cross-check the default, most predictable path.
- **Default `top_k`:** 3 (crate default).
- **`seasonal_period`:** reuse the existing `ts_forecast_by` `seasonal_period` param; `0` → non-seasonal (`None`), `p>1` → seasonal AutoARIMA/AutoETS/AutoTheta.
- **Stacking `folds`:** fixed internally (default in crate is whatever `Stacking { folds }` is called with), not exposed in SQL for v1.
- **Candidate families:** fixed to ARIMA/ETS/Theta — crate-defined in `AutoEnsemble::fit`, not user-configurable in Phase 4.
- **Fewer than `top_k` families fit:** combine whatever fitted successfully (crate already handles via `min(top_k, len)`); propagate error only when no model fits (`ConvergenceFailure`).

### Claude's Discretion
- Exact param key names inside the MAP (e.g. `top_k` vs `k`), string-normalization helper, and where the enum-string parse lives (FFI vs C++) — follow existing GARCH/Kalman convention.
- Whether the FFI carries `top_k` as `c_int` and `combination_method` as a fixed-size `[c_char; N]` (consistent with `kalman_model`) is an implementation detail at Claude's discretion.

### Deferred Ideas (OUT OF SCOPE)
- Explicit user-named member ensemble → Phase 5 (ENS-02).
- Ensemble prediction intervals via conformal path → Phase 6 (EPI-01).
- Member/weight introspection surface → Phase 6 (INSP-01).
- Custom hand-supplied weights (`CombinationMethod::Custom`, ENS-F1) and panel/VAR ensembles (ENS-F2) — deferred beyond v0.8.0.
- Exposing Stacking `folds` in SQL — deferred.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| ENS-01 | User can produce an AutoEnsemble forecast per series (auto-fits ARIMA/ETS/Theta, ranks, combines top-K); user sets `top_k`, `combination_method`, `seasonal_period`. | Crate `AutoEnsemble`/`AutoEnsembleConfig` API confirmed; wiring path through `ModelType` → `forecast()` → `extract_forecast()` documented below. |
| COMB-01 | User can select Mean or Median combination. | Both are unit-weight combiners — no weight computation needed; `CombinationMethod::Mean` and `::Median` verified in `model.rs`. |
| COMB-02 | User can select WeightedMSE or InverseAIC combination. | Both verified in `model.rs`; weight computation via `compute_mse_weights` / `compute_aic_weights`. |
| COMB-03 | User can select Stacking combination. | `CombinationMethod::Stacking { folds: usize }` verified; phase hard-codes `folds: 2` (or any fixed value). |
| COMB-04 | User can select HorizonAdaptive combination. | `CombinationMethod::HorizonAdaptive` verified; per-horizon weight matrix stored in `horizon_weights`. |
</phase_requirements>

---

## Summary

Phase 4 wires the existing crate-level `AutoEnsemble` (auto-fits AutoARIMA, AutoETS, AutoTheta; ranks by in-sample MSE; combines top-K) into the SQL surface via the well-established method-string dispatch path. The delivery is additive at every layer — no existing code is removed or restructured.

The three change sites are:

1. **`crates/anofox-fcst-core/src/forecast.rs`** — add `AutoEnsemble` to the `ModelType` enum, its `from_str`/`name` impls, the `ForecastOptions` struct (`top_k`/`combination_method` fields), and a `forecast_auto_ensemble()` helper that mirrors `forecast_kalman()`.

2. **`crates/anofox-fcst-ffi/src/types.rs`** — extend `ForecastOptions` (FFI) and `ForecastOptionsExog` (FFI) structs with two additive fields (`ensemble_top_k: c_int`, `ensemble_method: [c_char; 32]`); extend `build_core_options` in `lib.rs` to parse and thread them.

3. **`src/scalar_functions/ts_forecast_scalar.cpp`** and **`src/table_functions/ts_forecast_native.cpp`** — add two new parameter keys (`top_k`, `combination_method`) to the allowed-keys set, the local bind-data structs, and the opts-building block. No new C++ source files needed.

The `ts_forecast_by` macro in `src/macros/ts_macros.cpp` requires no changes — it passes `params` MAP/STRUCT through as-is.

**Primary recommendation:** Follow the GARCH/Kalman additive-field precedent exactly. The only non-trivial decision is the string-to-enum parse location; by precedent (Kalman's `kalman_model`) this lives in `anofox_fcst_core::forecast()`, not in C++.

---

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| AutoEnsemble model construction | Rust Core (`forecast.rs`) | — | All model construction happens in the core layer, not in C++ |
| CombinationMethod string→enum parse | Rust Core (`forecast.rs`) | — | Follows `kalman_model` precedent: C++ passes raw string, Rust parses |
| Parameter key validation | C++ (`ts_forecast_scalar.cpp`, `ts_forecast_native.cpp`) | — | Both files have a `ValidateParams` / `ValidateParamKeys` set that must be updated |
| FFI struct extension | Rust FFI (`types.rs`, `lib.rs`) | — | Struct fields are `#[repr(C)]`; additive fields appended at the end |
| SQL surface | SQL Macro (`ts_macros.cpp`) | — | No changes needed; macro passes `params` MAP/STRUCT through unchanged |

---

## Crate API Surface (anofox-forecast 0.15.3)

### AutoEnsemble and AutoEnsembleConfig

[VERIFIED: ~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/ensemble/auto.rs:16-55]

```rust
pub struct AutoEnsembleConfig {
    pub top_k: usize,
    pub combination_method: CombinationMethod,
    pub seasonal_period: Option<usize>,
}

impl Default for AutoEnsembleConfig {
    fn default() -> Self {
        Self {
            top_k: 3,
            combination_method: CombinationMethod::WeightedMSE,  // crate default; CONTEXT overrides to Mean
            seasonal_period: None,
        }
    }
}
```

Construction pattern for the FFI wrapper:
```rust
let config = AutoEnsembleConfig {
    top_k: top_k_from_opts,                    // e.g. 3
    combination_method: parsed_method,          // e.g. CombinationMethod::Mean
    seasonal_period: if period > 1 { Some(period) } else { None },
};
let mut model = AutoEnsemble::with_config(config);
```

### AutoEnsemble Forecaster Trait Implementation

[VERIFIED: ~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/ensemble/auto.rs:134-253]

- `fn fit(&mut self, series: &TimeSeries) -> Result<()>` — fits AutoARIMA, AutoETS, AutoTheta; sorts by MSE ascending; takes top-K; creates `Ensemble` and calls `ensemble.fit(series)`.
- `fn predict(&self, horizon: usize) -> Result<Forecast>` — delegates to `self.ensemble.as_ref()?.predict(horizon)`.
- `fn predict_with_intervals(&self, horizon: usize, level: f64) -> Result<Forecast>` — delegates to `self.ensemble.as_ref()?.predict_with_intervals(horizon, level)`. The Ensemble `predict_with_intervals` returns a widest-envelope interval (min of lowers, max of uppers across component models that produce intervals).
- `fn fitted_values(&self) -> Option<&[f64]>` — returns combined fitted values (populated after `fit`).
- `fn residuals(&self) -> Option<&[f64]>` — returns combined residuals (populated after `fit`).
- `fn name(&self) -> &str` — returns `"AutoEnsemble"` (static string).
- `fn is_fitted(&self) -> bool` — `true` after successful `fit`.
- `fn all_scores(&self) -> &[(String, f64)]` — returns name+MSE for all three candidate models, sorted ascending. Available for Phase 6 introspection. **Do not expose in Phase 4** — store-and-forward only.
- `fn model_count(&self) -> usize` — number of members in the final ensemble (≤ top_k).

**Error path:** If no model fits at all, `fit` returns `ForecastError::ConvergenceFailure("No models could be fitted successfully".into())`. The FFI boundary must propagate this as a DuckDB exception.

**Fewer-than-top_k:** handled inside `fit` via `top_k.min(candidates.len())` — not an error.

### CombinationMethod Enum

[VERIFIED: ~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/ensemble/model.rs:13-45]

Verbatim enum definition:
```rust
pub enum CombinationMethod {
    Mean,
    Median,
    WeightedMSE,
    Custom,
    InverseAIC,
    Stacking {
        folds: usize,
    },
    HorizonAdaptive,
}
```

`Custom` is NOT exposed in Phase 4 (deferred, ENS-F1).

### Ensemble Accessors (for Phase 6 — do not surface in Phase 4)

[VERIFIED: ~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/ensemble/model.rs:202-219]

```rust
pub fn weights(&self) -> &[f64]
pub fn horizon_weights(&self) -> Option<&Vec<Vec<f64>>>
pub fn method(&self) -> CombinationMethod
pub fn model_count(&self) -> usize
```

The `Ensemble` struct is only reachable through `AutoEnsemble.ensemble` (private field). Phase 6 introspection will need to access it; the Phase 4 Rust function should store the fitted `AutoEnsemble` temporarily to read `all_scores()` and `model_count()` before discarding, if needed. In Phase 4 only the forecast output matters.

---

## FFI Extension Point

### Current ForecastOptions (FFI struct)

[VERIFIED: crates/anofox-fcst-ffi/src/types.rs:372-414]

Verbatim existing struct fields (appended after `kalman_model` at line 413):
```rust
#[repr(C)]
pub struct ForecastOptions {
    pub model: [c_char; 32],
    pub ets_model: [c_char; 8],
    pub horizon: c_int,
    pub confidence_level: c_double,
    pub seasonal_period: c_int,
    pub auto_detect_seasonality: bool,
    pub include_fitted: bool,
    pub include_residuals: bool,
    pub window: c_int,
    pub seasonal_periods_str: [c_char; 64],
    pub model_pool: [c_char; 32],
    pub laplace_variant: [c_char; 16],
    pub laplace_seasonal_batch_init: bool,
    pub garch_p: c_int,          // line 407
    pub garch_q: c_int,          // line 409
    pub kalman_model: [c_char; 32],  // line 411–413
    // ↑ ADD NEW FIELDS HERE (additive at end)
}
```

**New fields to add (additive, after `kalman_model`):**
```rust
    /// AutoEnsemble: number of top models to select (0 → default 3).
    /// Only consulted when model is "AutoEnsemble".
    pub ensemble_top_k: c_int,
    /// AutoEnsemble: combination method string.
    /// Accepted: "" | "mean" | "median" | "weighted_mse" | "inverse_aic" | "stacking" | "horizon_adaptive"
    /// (and common aliases). Empty = "mean" (Phase 4 default). Only consulted when model is "AutoEnsemble".
    pub ensemble_method: [c_char; 32],
```

Identical additions must be made to `ForecastOptionsExog` [VERIFIED: crates/anofox-fcst-ffi/src/types.rs:537-573] — same field positions at the end of that struct.

**C++ `memset` safety:** Both C++ callsites do `memset(&opts, 0, sizeof(opts))` before filling fields [VERIFIED: src/scalar_functions/ts_forecast_scalar.cpp:452-453, src/table_functions/ts_forecast_native.cpp:661 (implicit in build_core_options pattern)]. Zero-initializing `ensemble_top_k` leaves it as 0 (→ default 3) and `ensemble_method` as all-NUL (→ empty string → default "mean"). No existing callers need changes.

### FFI Dispatch: anofox_ts_forecast

[VERIFIED: crates/anofox-fcst-ffi/src/lib.rs:3386-3478]

The `anofox_ts_forecast` function:
1. Reads `model_str` from `opts.model` (CStr).
2. Parses it via `model_str.parse::<anofox_fcst_core::ModelType>()`.
3. Builds `anofox_fcst_core::ForecastOptions { model: model_type, ... }` and calls `anofox_fcst_core::forecast(&series, &core_opts)`.

**Two places to extend in lib.rs:**

**A. In `anofox_ts_forecast` (line ~3452–3476):** parse the two new fields from `opts` and thread them into `core_opts`:
```rust
let ensemble_method = CStr::from_ptr(opts.ensemble_method.as_ptr())
    .to_str()
    .ok()
    .filter(|s| !s.is_empty())
    .map(str::to_owned);

let core_opts = anofox_fcst_core::ForecastOptions {
    // ... existing fields ...
    ensemble_top_k: opts.ensemble_top_k as usize,
    ensemble_method,
};
```

**B. In `build_core_options` (line ~4118–4177, used by aggregation path):** identical parsing.

### ForecastOptions in anofox-fcst-core

[VERIFIED: crates/anofox-fcst-core/src/forecast.rs:331-396]

Add two fields to `ForecastOptions` (core):
```rust
pub struct ForecastOptions {
    // ... existing fields ...
    pub ensemble_top_k: usize,        // 0 → default 3
    pub ensemble_method: Option<String>, // None → "mean"
}
```

And to the `Default` impl:
```rust
ensemble_top_k: 0,
ensemble_method: None,
```

And to the `From<ForecastOptions> for ForecastOptionsExog` impl — add passthrough.

---

## ModelType Extension (anofox-fcst-core/src/forecast.rs)

### Step 1: Add enum variant

[VERIFIED: crates/anofox-fcst-core/src/forecast.rs:95-157]

Add `AutoEnsemble` to the `ModelType` enum after `Kalman` (line 156):
```rust
    Kalman,
    /// AutoEnsemble: auto-fits AutoARIMA/AutoETS/AutoTheta, ranks by in-sample MSE,
    /// combines top-K members using the specified CombinationMethod.
    AutoEnsemble,
```

### Step 2: `from_str` — exact-match arm (after "Kalman" arm at line 209)

[VERIFIED: crates/anofox-fcst-core/src/forecast.rs:208-210]

```rust
"AutoEnsemble" => return Ok(ModelType::AutoEnsemble),
```

Case-insensitive fallback in the lowercase block:
```rust
"autoensemble" | "auto_ensemble" => Ok(ModelType::AutoEnsemble),
```

### Step 3: `name()` method (after "Kalman" arm at line ~324)

```rust
ModelType::AutoEnsemble => "AutoEnsemble",
```

### Step 4: `forecast()` match arm (after `ModelType::Kalman` arm at line 739–743)

```rust
ModelType::AutoEnsemble => forecast_auto_ensemble(
    &clean_values,
    options.horizon,
    if options.ensemble_top_k == 0 { 3 } else { options.ensemble_top_k },
    options.ensemble_method.as_deref(),
    period,
),
```

### Step 5: `forecast_auto_ensemble()` helper function

Follows `forecast_kalman()` pattern exactly [VERIFIED: crates/anofox-fcst-core/src/forecast.rs:2453-2463]:

```rust
use anofox_forecast::models::ensemble::{AutoEnsemble, AutoEnsembleConfig};
use anofox_forecast::models::ensemble::model::CombinationMethod;

fn forecast_auto_ensemble(
    values: &[f64],
    horizon: usize,
    top_k: usize,
    method_str: Option<&str>,
    period: usize,
) -> Result<ForecastOutput> {
    let combination_method = parse_combination_method(method_str)?;
    let config = AutoEnsembleConfig {
        top_k,
        combination_method,
        seasonal_period: if period > 1 { Some(period) } else { None },
    };
    let ts = make_timeseries(values)?;
    let mut model = AutoEnsemble::with_config(config);
    model.fit(&ts).map_err(|e| {
        ForecastError::ComputationError(format!("AutoEnsemble fit failed: {}", e))
    })?;
    extract_forecast(&model, horizon, "AutoEnsemble")
}
```

---

## Combination Method String Parse

### Where: in `anofox-fcst-core/src/forecast.rs`

Following the `kalman_model` precedent: C++ passes a raw string through `ensemble_method: [c_char; 32]`; Rust parses it in the core layer. This keeps validation centralized and error messages consistent.

### Parse function

```rust
fn parse_combination_method(s: Option<&str>) -> Result<CombinationMethod> {
    match s.unwrap_or("").trim().to_lowercase().as_str() {
        "" | "mean" => Ok(CombinationMethod::Mean),
        "median" => Ok(CombinationMethod::Median),
        "weighted_mse" | "weightedmse" | "weighted-mse" => Ok(CombinationMethod::WeightedMSE),
        "inverse_aic" | "inverseaic" | "inverse-aic" | "aic" => Ok(CombinationMethod::InverseAIC),
        "stacking" | "stack" => Ok(CombinationMethod::Stacking { folds: 2 }),
        "horizon_adaptive" | "horizonadaptive" | "horizon-adaptive" | "adaptive" => {
            Ok(CombinationMethod::HorizonAdaptive)
        }
        other => Err(ForecastError::InvalidParameter {
            param: "combination_method".to_string(),
            value: other.to_string(),
            reason: "expected one of: mean, median, weighted_mse, inverse_aic, stacking, horizon_adaptive".to_string(),
        }),
    }
}
```

**String-to-enum accepted values table:**

| SQL string(s) | CombinationMethod variant | Notes |
|---|---|---|
| `''` (empty), `'mean'` | `Mean` | Default |
| `'median'` | `Median` | |
| `'weighted_mse'`, `'weightedmse'`, `'weighted-mse'` | `WeightedMSE` | |
| `'inverse_aic'`, `'inverseaic'`, `'inverse-aic'`, `'aic'` | `InverseAIC` | |
| `'stacking'`, `'stack'` | `Stacking { folds: 2 }` | `folds` fixed at 2, not exposed |
| `'horizon_adaptive'`, `'horizonadaptive'`, `'horizon-adaptive'`, `'adaptive'` | `HorizonAdaptive` | |
| `'custom'` | NOT accepted | `Custom` is deferred (ENS-F1); return `InvalidParameter` |

`folds: 2` is used because the crate's `compute_stacking_weights` currently ignores the `folds` field entirely (it hard-codes the second-half holdout split) [VERIFIED: model.rs:397-474]. Any non-zero value is equivalent at runtime; 2 is a reasonable documentation signal.

---

## C++ Layer: What to Touch

### ts_forecast_scalar.cpp

[VERIFIED: src/scalar_functions/ts_forecast_scalar.cpp:127-165, 421-488]

**Three locations in this file:**

**A. `ValidateParams` valid_keys set (line 129):** add `"top_k"` and `"combination_method"`:
```cpp
static const unordered_set<string> valid_keys = {
    "model", "seasonal_period", "seasonal_periods", "confidence_level", "window", "model_pool",
    "laplace_variant", "laplace_seasonal_batch_init",
    "garch_p", "garch_q", "kalman_model",
    "top_k", "combination_method"   // Phase 4: AutoEnsemble params
};
```
And update the error message string to include the new keys.

**B. Local variable declarations in `TsForecastScalarExecute` (line ~421):** add after `kalman_model`:
```cpp
int64_t ensemble_top_k = bind_data.ensemble_top_k;
string ensemble_method = bind_data.ensemble_method;
```

**C. MAP/STRUCT params parsing block (line ~446–448):** add after `kalman_model`:
```cpp
ensemble_top_k = ParseInt64Param(params_val, "top_k", 0);
ensemble_method = ParseStringParam(params_val, "combination_method", "");
```

**D. ForecastOptions opts building (line ~482–488):** add after `kalman_model`:
```cpp
opts.ensemble_top_k = static_cast<int>(ensemble_top_k);
if (!ensemble_method.empty()) {
    strncpy(opts.ensemble_method, ensemble_method.c_str(),
            sizeof(opts.ensemble_method) - 1);
    opts.ensemble_method[sizeof(opts.ensemble_method) - 1] = '\0';
}
```

**E. `TsForecastScalarBindData` struct (line ~50–85, inferred from bind_data usage):** add two fields:
```cpp
int64_t ensemble_top_k = 0;
string ensemble_method = "";
```

### ts_forecast_native.cpp

[VERIFIED: src/table_functions/ts_forecast_native.cpp:33-59, 274-310, 360-363, 661-666]

**Same four locations with identical changes:**

**A. `TsForecastNativeBindData` struct (line 33–59):** add after `kalman_model`:
```cpp
// AutoEnsemble params (Phase 4)
int64_t ensemble_top_k = 0;
string ensemble_method = "";
```

**B. `ValidateParamKeys` valid_keys set (line 275–278):** add `"top_k"` and `"combination_method"`. Update error message.

**C. Bind function params parsing (line 361–363):** add after `kalman_model` lines:
```cpp
bind_data->ensemble_top_k = ParseInt64FromParams(params, "top_k", 0);
bind_data->ensemble_method = ParseStringFromParams(params, "combination_method", "");
```

**D. opts building in Finalize/Execute (line 661–666):** add after `kalman_model` block:
```cpp
opts.ensemble_top_k = static_cast<int>(bind_data.ensemble_top_k);
if (!bind_data.ensemble_method.empty()) {
    strncpy(opts.ensemble_method, bind_data.ensemble_method.c_str(),
            sizeof(opts.ensemble_method) - 1);
    opts.ensemble_method[sizeof(opts.ensemble_method) - 1] = '\0';
}
```

### ts_macros.cpp — NO CHANGES REQUIRED

[VERIFIED: src/macros/ts_macros.cpp:575-594]

The `ts_forecast_by` macro passes `params` MAP/STRUCT through to `_ts_forecast_scalar` verbatim. Adding new keys to the validated set inside the scalar function is sufficient.

### CMakeLists.txt — NO NEW SOURCES

This phase adds only new fields and code branches to existing files. No new `.cpp` source file is created, so CMakeLists does not need updating.

### cbindgen header regeneration

After editing `crates/anofox-fcst-ffi/src/types.rs`, the C header must be regenerated:
```bash
make header
```
This runs cbindgen per `crates/anofox-fcst-ffi/cbindgen.toml` and updates `anofox_fcst_ffi.h`. The C++ files include this header, so the new `ensemble_top_k` and `ensemble_method` fields are available.

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Combination weight computation | Custom weighted-average logic | `CombinationMethod` variants in crate | All six methods (incl. stacking gradient descent, horizon-adaptive rolling origin) are already implemented and tested |
| Model ranking | Sort-by-MSE in C++ or FFI | `AutoEnsemble::fit` — already sorts `candidates` by MSE | Re-implementing ranking risks tie-breaking divergence |
| Simplex projection for stacking | Custom NNLS | `nnls_simplex()` in `model.rs` | Duchi et al. (2008) algorithm already implemented |
| String-to-enum parse | `if/else if` in C++ | `parse_combination_method()` Rust function | Keeps validation in one place, error type is consistent |

---

## Architecture Patterns

### System Architecture Diagram

```
SQL: ts_forecast_by(..., 'AutoEnsemble', 12, '1d', {top_k: 2, combination_method: 'stacking'})
  │
  ▼
src/macros/ts_macros.cpp
  ts_forecast_by → _ts_forecast_scalar(dates, values, horizon, freq, method, params)
  │
  ▼
src/scalar_functions/ts_forecast_scalar.cpp
  ValidateParams (checks top_k, combination_method in valid_keys)
  Parse params → ensemble_top_k, ensemble_method
  Build ForecastOptions opts (memset 0; fill method="AutoEnsemble", ensemble_top_k, ensemble_method)
  anofox_ts_forecast(&opts, ...)
  │
  ▼
crates/anofox-fcst-ffi/src/lib.rs  (anofox_ts_forecast)
  parse model_str → ModelType::AutoEnsemble
  parse ensemble_method → Option<String>
  build core_opts with ensemble_top_k, ensemble_method
  anofox_fcst_core::forecast(&series, &core_opts)
  │
  ▼
crates/anofox-fcst-core/src/forecast.rs  (forecast())
  match ModelType::AutoEnsemble → forecast_auto_ensemble(values, horizon, top_k, method_str, period)
    parse_combination_method(method_str) → CombinationMethod::Stacking { folds: 2 }
    AutoEnsemble::with_config(AutoEnsembleConfig { top_k: 2, combination_method: Stacking{2}, seasonal_period: None })
    model.fit(&ts)     ← fits AutoARIMA + AutoETS + AutoTheta, takes top-2
    extract_forecast(&model, horizon, "AutoEnsemble")
      model.predict(horizon) → Forecast::from_values(combined_stacking_forecast)
  │
  ▼
ForecastResult { point_forecasts, lower_bounds=null, upper_bounds=null, model_name="AutoEnsemble" }
  │
  ▼
ts_forecast_scalar → LIST(STRUCT(forecast_step, ds, yhat, yhat_lower=NULL, yhat_upper=NULL, model_name))
  │
  ▼
SQL result: group_col | forecast_step | ds | yhat | yhat_lower | yhat_upper | model_name
```

### Recommended Project Structure (no new files for Phase 4)

All changes are in-place edits to existing files:
```
crates/anofox-fcst-core/src/forecast.rs  ← ModelType, ForecastOptions, run_forecast, new helper
crates/anofox-fcst-ffi/src/types.rs      ← ForecastOptions (FFI), ForecastOptionsExog (FFI)
crates/anofox-fcst-ffi/src/lib.rs        ← anofox_ts_forecast, build_core_options parsing
src/scalar_functions/ts_forecast_scalar.cpp  ← bind data, valid_keys, parse, opts-build
src/table_functions/ts_forecast_native.cpp   ← bind data, valid_keys, parse, opts-build
examples/autoensemble.sql               ← NEW (DoD requirement)
docs/api/07-forecasting.md              ← update to document AutoEnsemble
docs/reference/models/ensemble/autoensemble.md  ← NEW (DoD requirement)
```

---

## Prediction Intervals Behaviour

**Key finding:** `AutoEnsemble.predict()` returns point forecasts only (no intervals). `AutoEnsemble.predict_with_intervals()` delegates to `Ensemble.predict_with_intervals()`, which returns a widest-envelope interval (min of lowers, max of uppers across component models).

[VERIFIED: ~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/ensemble/model.rs:675-725]

**Phase 4 handling:** `extract_forecast()` calls `model.predict(horizon)`, which returns point forecasts only via `Forecast::from_values(combined)`. The `lower` and `upper` fields in `ForecastOutput` will be empty `vec![]`, which causes the C++ layer to emit `NULL` for `yhat_lower`/`yhat_upper` (already handled by the existing null-check in `ts_forecast_scalar.cpp` lines 533–540).

This is correct for Phase 4. Ensemble prediction intervals are Phase 6 (EPI-01).

**`include_fitted`/`include_residuals` interaction:** `extract_forecast()` always populates `fitted` and `residuals` from `model.fitted_values()`/`model.residuals()` regardless of `include_fitted` — it does not gate on those flags. The C++ callsites set `opts.include_fitted = false` and `opts.include_residuals = false`, but this has no effect for models routed through `extract_forecast()` (only the `calculate_fitted_values` path in `forecast()` uses those flags). This is consistent with existing behavior for AutoARIMA/AutoETS/AutoTheta.

---

## Verification / Cross-Check Approach (DoD)

### Internal-consistency cross-check: Mean combination == arithmetic mean of member forecasts

The CONTEXT specifies that with `combination_method='mean'`, the ensemble point forecast must equal the arithmetic mean of the three member model forecasts computed independently.

**How to run the cross-check in the example SQL:**

1. Run AutoEnsemble with `top_k=3`, `combination_method='mean'`, and a specific `seasonal_period` so that all three members use the same seasonality:
```sql
SELECT * FROM ts_forecast_by('series', id, ds, y, 'AutoEnsemble', 12, '1d',
    {top_k: 3, combination_method: 'mean', seasonal_period: 0});
```

2. Run AutoARIMA, AutoETS, AutoTheta independently on the same series:
```sql
SELECT 'AutoARIMA' AS m, avg(yhat) AS mean_yhat FROM ts_forecast_by('series', id, ds, y, 'AutoARIMA', 12, '1d') GROUP BY forecast_step;
-- repeat for AutoETS, AutoTheta
```

3. Average the three independently:
```sql
SELECT forecast_step, (arima_yhat + ets_yhat + theta_yhat) / 3.0 AS manual_mean
FROM ...;
```

4. Compare against AutoEnsemble with `combination_method='mean'`.

**Crucial subtlety — when the cross-check is valid:**

The cross-check is exact **only when all three models converge** (i.e., `model_count() == 3` after fit). If one fails to converge, AutoEnsemble selects `min(top_k=3, candidates=2) = 2` members, and the manual average of all three members will NOT match — because the failed model is excluded from the ensemble but included in the manual average.

**Recommendation for the example:** Use a clean, long synthetic series (≥ 60 observations) with zero nulls and moderate trend so all three Auto* models are virtually certain to converge. Document this constraint in the example comments.

**How to confirm `model_count == 3`:** Phase 6 will expose introspection. For Phase 4, the `model_name` output column returns `"AutoEnsemble"` (not `"Ensemble (Mean)"`) — no model count is visible in Phase 4. The example should use a series that reliably yields all three fits (the crate test suite at `auto.rs:284` uses 60 linearly-trended observations and passes reliably).

**Mean combination formula in crate:** [VERIFIED: model.rs:263–268]
```rust
CombinationMethod::Mean => {
    for h in 0..horizon {
        let sum: f64 = values.iter().filter(|v| h < v.len()).map(|v| v[h]).sum();
        combined[h] = sum / values.len() as f64;
    }
}
```
This is a simple unweighted mean. `values.len()` = number of models that actually produced forecasts (all fitted members), which equals `model_count`.

**Mean vs Median demonstrability (COMB-01):** Use a skewed series (e.g., exponential growth with outlier spikes) where member forecasts diverge. Run both methods and show `yhat` values differ. The example must show visibly different numbers, not just claim they differ.

---

## Common Pitfalls

### Pitfall 1: ABI Break from Middle-Insertion of Struct Fields

**What goes wrong:** Adding new `#[repr(C)]` fields in the middle of `ForecastOptions` shifts the offset of subsequent fields, silently corrupting any caller that was compiled against the old layout.

**Why it happens:** C structs are laid out sequentially; inserting at position N moves everything after N.

**How to avoid:** Always append new fields at the END of the struct. Both `ForecastOptions` (FFI) and `ForecastOptionsExog` (FFI) must be extended by appending `ensemble_top_k` and `ensemble_method` after the existing last field `kalman_model`. [VERIFIED: types.rs:411–413]

**Warning sign:** Kalman or GARCH tests producing wrong values after the change.

### Pitfall 2: C++ memset Compatibility for New Fields

**What goes wrong:** If the struct size changes and C++ callers use `memset(&opts, 0, sizeof(opts))`, the new fields are zero-initialized automatically — which is correct (0 → default for `ensemble_top_k`, NUL → empty string → default for `ensemble_method`). No action required. BUT if any callsite uses a hardcoded size or a cached `sizeof`, it will miss the new fields.

**How to avoid:** Verify all callsites use `sizeof(ForecastOptions)` (not a constant). [VERIFIED: ts_forecast_scalar.cpp:452: `memset(&opts, 0, sizeof(opts))`] — correct.

### Pitfall 3: Forgetting to Update Both ForecastOptions AND ForecastOptionsExog

**What goes wrong:** `ForecastOptionsExog` (used by the exogenous path) is a separate struct with the same fields as `ForecastOptions` plus `exog`. If `ensemble_*` fields are added to `ForecastOptions` but not `ForecastOptionsExog`, the exogenous forecast path (if ever triggered for AutoEnsemble) will not forward them.

**How to avoid:** Add fields to both structs in `types.rs` and update both `Default` impls and the `From<ForecastOptions> for ForecastOptionsExog` conversion. [VERIFIED: types.rs:537–573 shows the struct; lib.rs ~4118 shows `build_core_options` uses `ForecastOptions`, not `ForecastOptionsExog` — but `From` impl at core/forecast.rs:530–551 must propagate new fields.]

### Pitfall 4: include_fitted/include_residuals have NO effect for AutoEnsemble

**What goes wrong:** The executor (or a future reviewer) sets `include_fitted = true` expecting to get fitted values for AutoEnsemble. The flag is ignored by `extract_forecast()` — AutoEnsemble's fitted values come from `model.fitted_values()` directly.

**Why it happens:** `extract_forecast()` unconditionally pulls from the `Forecaster` trait's `fitted_values()`; the `include_fitted` flag only gates the older `calculate_fitted_values()` code path used for non-trait-based models. [VERIFIED: forecast.rs:2341–2351]

**How to avoid:** The C++ code correctly sets `opts.include_fitted = false; opts.include_residuals = false;` for all models. No change needed.

### Pitfall 5: AutoEnsemble seasonal_period=0 and the Top-K Tie-breaking Subtlety

**What goes wrong:** With `seasonal_period=0`, AutoEnsemble passes `seasonal_period: None` to each sub-model. This makes AutoARIMA, AutoETS, AutoTheta each fit non-seasonally. If the user later runs individual member models with `seasonal_period=7`, their forecasts will NOT match the ensemble members' forecasts (different configuration).

**How to avoid:** The cross-check example must use the same `seasonal_period` value in both the ensemble call and the individual member calls. Document this in the example.

**Tie-breaking in candidate selection:** [VERIFIED: auto.rs:198]: `candidates.sort_by(|a, b| a.2.partial_cmp(&b.2).unwrap_or(std::cmp::Ordering::Equal))` — ascending MSE sort, ties broken by `Equal` (no stable ordering guarantee). With `top_k=3` and all three candidates fitting, this is irrelevant. With `top_k=2`, which two are selected depends on MSE order.

### Pitfall 6: WASM Build (LINKED_LIBS trap)

Already handled at project level for the FFI crate [VERIFIED in project memory: project_extension_wasm_linked_libs.md]. The `LINKED_LIBS` configuration in `extension_config.cmake` ensures Rust static archives are not silently dropped on WASM. Phase 4 adds no new crate features or static libs, so no additional WASM action is needed beyond confirming the existing mechanism covers the ensemble code.

### Pitfall 7: ConvergenceFailure Error Propagation

**What goes wrong:** If all three Auto* models fail to fit (e.g., constant series, too-short series), `AutoEnsemble::fit` returns `ForecastError::ConvergenceFailure("No models could be fitted successfully".into())`. The FFI error-handling path must surface this as a DuckDB exception rather than silently returning null forecasts.

[VERIFIED: auto.rs:191–195]
```rust
if candidates.is_empty() {
    return Err(ForecastError::ConvergenceFailure(
        "No models could be fitted successfully".into(),
    ));
}
```

**How to avoid:** The existing error propagation in `anofox_ts_forecast` (lib.rs ~3481) passes `Err(e)` to `(*out_error).set_error(ErrorCode::ComputationError, &e.to_string())` → C++ throws `InvalidInputException`. This is already correct for ConvergenceFailure; no special handling needed.

---

## Validation Architecture

### Internal-Consistency Cross-Check (DoD)

The DoD requires a runnable SQL cross-check verifying that `Mean` combination == arithmetic mean of member forecasts.

**Test structure for `examples/autoensemble.sql`:**

```sql
-- Synthetic 60-observation series (all three members reliably fit)
CREATE OR REPLACE TABLE ae_test AS
SELECT i AS id, '2020-01-01'::DATE + INTERVAL (i-1) DAY AS ds, 10.0 + i * 0.5 AS y
FROM generate_series(1, 60) t(i);

-- Step 1: AutoEnsemble Mean (top_k=3, non-seasonal)
CREATE OR REPLACE TABLE ae_mean AS
SELECT * FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 5, '1d',
    {top_k: 3, combination_method: 'mean', seasonal_period: 0});

-- Step 2: Individual member forecasts
CREATE OR REPLACE TABLE ae_arima  AS SELECT forecast_step, yhat AS y_arima  FROM ts_forecast_by('ae_test', id, ds, y, 'AutoARIMA', 5, '1d');
CREATE OR REPLACE TABLE ae_ets    AS SELECT forecast_step, yhat AS y_ets    FROM ts_forecast_by('ae_test', id, ds, y, 'AutoETS',   5, '1d');
CREATE OR REPLACE TABLE ae_theta  AS SELECT forecast_step, yhat AS y_theta  FROM ts_forecast_by('ae_test', id, ds, y, 'AutoTheta', 5, '1d');

-- Step 3: Manual mean
CREATE OR REPLACE TABLE ae_manual AS
SELECT a.forecast_step,
       (y_arima + y_ets + y_theta) / 3.0 AS manual_mean
FROM ae_arima a JOIN ae_ets e USING (forecast_step) JOIN ae_theta t USING (forecast_step);

-- Step 4: Cross-check (tolerance 1e-6 for floating-point)
SELECT
    m.forecast_step,
    m.yhat AS ensemble_mean,
    n.manual_mean,
    abs(m.yhat - n.manual_mean) < 1e-6 AS match
FROM ae_mean m JOIN ae_manual n USING (forecast_step)
ORDER BY forecast_step;
-- All rows must have match=true
```

**Six-method smoke test:** One SQL block per COMB variant; each must return a non-NULL, finite `yhat` for each forecast step.

### No Nyquist validation section needed

`workflow.nyquist_validation` is not present in `.planning/config.json` for this project (project relies on the DoD-example pattern instead). The validation architecture above is the DoD definition.

---

## Sources

### Primary (HIGH confidence — source files read this session)

- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/ensemble/auto.rs` (1–326) — AutoEnsemble, AutoEnsembleConfig, Forecaster impl
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/ensemble/model.rs` (1–1411) — CombinationMethod enum, Ensemble impl, all six combination methods
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/ensemble/mod.rs` — module re-exports
- `crates/anofox-fcst-core/src/forecast.rs` (1–2465) — ModelType enum, ForecastOptions, `forecast()` match, `extract_forecast`, `make_timeseries`, `forecast_kalman` (precedent)
- `crates/anofox-fcst-ffi/src/types.rs` (360–573) — ForecastOptions (FFI), ForecastOptionsExog (FFI), existing additive fields
- `crates/anofox-fcst-ffi/src/lib.rs` (3386–3478) — `anofox_ts_forecast` dispatch; 4118–4177 — `build_core_options`
- `src/scalar_functions/ts_forecast_scalar.cpp` (85–488) — ValidateParams, TsForecastScalarExecute, opts building
- `src/table_functions/ts_forecast_native.cpp` (33–666) — TsForecastNativeBindData, ValidateParamKeys, Bind, opts building
- `src/macros/ts_macros.cpp` (565–594) — ts_forecast_by macro (confirmed no change needed)

---

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `folds: 2` is an acceptable fixed value for `Stacking` since the crate currently ignores `folds` and uses second-half holdout | CombinationMethod table | If a future crate version actually uses `folds`, SQL behavior will silently change without an ABI or API change — acceptable given "deferred" status |
| A2 | All three Auto* member models will fit on the 60-observation synthetic series used in the cross-check example | Validation | If any member fails (unlikely on linear trend), the cross-check will fail; use a more robust series if needed |

---

## Open Questions

1. **`ForecastOptionsExog` additive field propagation**
   - What we know: `From<ForecastOptions> for ForecastOptionsExog` exists at `crates/anofox-fcst-core/src/forecast.rs:530–551` and copies each field by name.
   - What's unclear: Whether `anofox_ts_forecast_with_exog` (the exogenous FFI function, ~lib.rs:3620) is ever called for method `AutoEnsemble`. Since AutoEnsemble has no exogenous support in Phase 4, this is moot.
   - Recommendation: Add the fields to `ForecastOptionsExog` for consistency and to avoid a future ABI surprise, but mark them as unused for AutoEnsemble in comments.

2. **`model_name` tag in output**
   - What we know: `extract_forecast(&model, horizon, "AutoEnsemble")` returns `model_name: "AutoEnsemble"`.
   - What's unclear: Should the output include the combination method (e.g., `"AutoEnsemble(Mean)"`)?
   - Recommendation: Start with plain `"AutoEnsemble"` for Phase 4 simplicity. The combination method can be added in Phase 6 alongside introspection.

---

## RESEARCH COMPLETE
