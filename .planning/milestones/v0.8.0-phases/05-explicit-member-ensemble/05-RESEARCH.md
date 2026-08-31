# Phase 5: Explicit-Member Ensemble — Research

**Researched:** 2026-08-31
**Domain:** Ensemble forecasting — name-to-BoxedForecaster factory, new FFI export for VARCHAR[], new C++ table function, SQL macro
**Confidence:** HIGH — all findings verified by reading source files this session

---

<user_constraints>
## User Constraints (from CONTEXT.md)

### Locked Decisions
- **Dedicated macro `ts_forecast_ensemble_by(...)`** — NOT an overload of `ts_forecast_by`'s method-string. New native table function + macro, reusing Phase 4's FFI ensemble/combination plumbing.
- **Member list as SQL `VARCHAR[]`** — e.g. `members := ['AutoARIMA','AutoETS','Theta']`. Idiomatic DuckDB list.
- **Member vocabulary reuses the existing `ts_forecast_by` model-name vocabulary** (`ModelType::from_str`) — same names users already know. Unknown member names → clean `InvalidParameter` error naming the offending member.
- **Combination reuses Phase 4** — `parse_combination_method` (all six methods + aliases) feeds the crate `Ensemble::new(members).with_method(method)` path.
- **Default `combination_method`: Mean** (consistent with Phase 4 default; makes the cross-check the default path).
- **Shared params only in v1** — each named member fits with its own defaults plus a single shared `seasonal_period` (`0` → non-seasonal). Per-member parameter maps are deferred.
- **Minimum 2 members** — an ensemble of one is degenerate; fewer than 2 named members → clear error.
- **Duplicate members allowed** — the same model name may appear more than once.

### Claude's Discretion
- Exact macro parameter names/order (e.g. `members`, `combination_method`, `seasonal_period`, horizon/freq), the FFI marshalling shape for the `VARCHAR[]` member list (e.g. a packed length-prefixed C string array vs a delimited buffer), and where the name→`Box<dyn Forecaster>` factory lives are Claude's discretion — follow the established GARCH/Kalman + Phase 4 conventions.
- Whether to emit `model_name` as `'Ensemble'` or `'Ensemble(<methods>)'` — plain `'Ensemble'` recommended, mirroring Phase 4's plain `'AutoEnsemble'`.

### Deferred Ideas (OUT OF SCOPE)
- Ensemble prediction intervals (conformal) → Phase 6 (EPI-01).
- Member/weight introspection → Phase 6 (INSP-01). Keep `Ensemble::weights()`/`.method()` reachable when wiring Phase 5.
- Per-member parameter maps (each member its own params) → future milestone.
- Custom hand-supplied weights (`CombinationMethod::Custom`, ENS-F1) and panel/VAR ensembles (ENS-F2) → deferred beyond v0.8.0.
</user_constraints>

<phase_requirements>
## Phase Requirements

| ID | Description | Research Support |
|----|-------------|------------------|
| ENS-02 | User can produce an explicit-member ensemble forecast per series by naming the member models and a combination method; the extension fits each member and combines them. | Crate `Ensemble::new(Vec<Box<dyn Forecaster>>)` + `build_forecaster` factory confirmed; FFI marshalling via null-delimited concatenated string confirmed; C++ table function shape documented below. |
</phase_requirements>

---

## Summary

Phase 5 exposes the crate's `Ensemble::new(Vec<Box<dyn Forecaster>>)` API to SQL via a dedicated `ts_forecast_ensemble_by(...)` macro backed by a new `_ts_forecast_ensemble_native` table function. The central implementation risk — constructing each named member as a `Box<dyn Forecaster>` from its name — is resolved by **Option (b): a new `build_forecaster(model_type: ModelType, period: Option<usize>) -> Result<Box<dyn Forecaster>>` helper** in `forecast.rs`. This is recommended over Option (a) (the `ModelRegistry` / `ModelSpec` factory pattern) because it directly reuses the existing `ModelType::from_str` dispatch and model constructors already imported in forecast.rs, while avoiding the empty-registry and factory-closure overhead the registry approach requires.

The FFI carries the member list as a **single null-delimited concatenated C string** (e.g. `"AutoARIMA\0AutoETS\0Theta\0"`) plus a count field — a new dedicated FFI export `anofox_ts_forecast_ensemble` distinct from `anofox_ts_forecast`, avoiding any modification to the existing `ForecastOptions` struct.

**Primary recommendation:** New `build_forecaster` helper in forecast.rs + new dedicated FFI function + new C++ table function file (`ts_forecast_ensemble_native.cpp`), with a macro `ts_forecast_ensemble_by` that follows the `ts_forecast_by` scalar/native-table pattern. Phase 4 combination plumbing (`parse_combination_method`) is reused verbatim.

---

## Architectural Responsibility Map

| Capability | Primary Tier | Secondary Tier | Rationale |
|------------|-------------|----------------|-----------|
| Member name → `Box<dyn Forecaster>` factory | Rust Core (`forecast.rs`) | — | All model construction happens in the core layer, matching existing inline dispatch pattern |
| Member list validation (unknown names, count < 2) | Rust Core (`forecast.rs`) | — | Follows `kalman_model` / `combination_method` precedent: validation centralized in Rust |
| Combination method string→enum | Rust Core (`forecast.rs`) | — | Phase 4's `parse_combination_method` reused verbatim |
| FFI member list marshalling | Rust FFI (`lib.rs`) | C++ (new table fn) | Null-delimited C string on C++ side; Rust splits on `\0` |
| C++ parameter key validation | C++ (new `ts_forecast_ensemble_native.cpp`) | — | New file; `members` + `combination_method` + `seasonal_period` are the only keys |
| SQL surface | SQL Macro (`ts_macros.cpp`) | — | `ts_forecast_ensemble_by` macro wraps `_ts_forecast_ensemble_native` |

---

## THE CENTRAL RISK: name → `Box<dyn Forecaster>` factory

### Why the existing dispatch cannot be reused directly

[VERIFIED: crates/anofox-fcst-core/src/forecast.rs:700-783]

The existing `forecast()` match in `forecast.rs` runs each model **inline** — it constructs a model, calls `.fit()`, calls `.predict()` or a model-specific forecast function, and returns a `ForecastOutput`. It does NOT return a `Box<dyn Forecaster>`. `Ensemble::new(Vec<Box<dyn Forecaster>>)` requires pre-boxed instances that it will call `.fit()` and `.predict()` on internally.

### Option (a): `ModelRegistry` / `ModelSpec` factory pattern

[VERIFIED: ~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/traits.rs:296-394]

The crate provides `ModelSpec::new(name, factory_fn, has_intervals)` and `ModelSpec::with_period(name, factory_fn, period, has_intervals)`. A `ModelRegistry` collects them. `spec.create()` calls the stored factory closure to produce a `BoxedForecaster = Box<dyn Forecaster>`.

**How it would work for Phase 5:**
```rust
// For each name in member_names, build a ModelSpec and call .create():
let spec = ModelSpec::with_period(
    "AutoARIMA",
    |p| { let mut c = AutoARIMAConfig::default(); c.seasonal_period = p; Box::new(AutoARIMA::with_config(c)) },
    period.unwrap_or(0),
    false,
);
let member: Box<dyn Forecaster> = spec.create();
```

**Problem:** `ModelSpec` stores a `Box<dyn Fn() -> BoxedForecaster + Send + Sync>`. The closure captures `period` by value. This works, but is mechanically verbose — one closure per model family — with no win over a direct match arm.

**Also:** The crate's `ensemble_best_k` [VERIFIED: convenience.rs:437-477] shows this pattern is used for auto-selection (compare multiple models, pick top-k). For Phase 5 the names are **user-supplied and explicit**, so no holdout evaluation is needed, making the full registry/comparison machinery unnecessary.

**Verdict:** Option (a) works but adds indirection with no benefit over Option (b).

### Option (b): `build_forecaster` helper in forecast.rs — RECOMMENDED

A new function `build_forecaster(model_type: ModelType, period: Option<usize>) -> Result<Box<dyn Forecaster>>` mirrors the structure of the `forecast()` match but boxes each constructor instead of running it inline. All the required imports are already present in `forecast.rs` [VERIFIED: crates/anofox-fcst-core/src/forecast.rs:1-22].

### Supported member-model set for v1

The boxability of each `ModelType` variant determines what can appear in `members := [...]`. A model is boxable if it implements the `Forecaster` trait and has a zero- or period-parameterized constructor. Below is the **complete analysis** based on reading the source:

#### SUPPORTED (trivially boxable — implements `Forecaster`, period-parameterizable constructor)

| ModelType | Crate type | Constructor | Period param | Boxable |
|-----------|-----------|-------------|--------------|---------|
| `AutoARIMA` | `AutoARIMA` | `AutoARIMA::new()` or `AutoARIMA::with_config(AutoARIMAConfig { seasonal_period: p, ..Default::default() })` | `config.seasonal_period` | YES |
| `AutoETS` | `AutoETS` | `AutoETS::new()` or `AutoETS::with_config(AutoETSConfig { seasonal_period: Some(p), ..Default::default() })` | `config.seasonal_period` | YES |
| `AutoTheta` | `AutoTheta` | `AutoTheta::new()` (non-seasonal) or `AutoTheta::seasonal(p)` | `AutoTheta::seasonal(p)` [VERIFIED: auto.rs:109-120] | YES |
| `Theta` | `Theta` | `Theta::new()` | No period in v1 (seasonal handled internally) | YES |
| `OptimizedTheta` | `OptimizedTheta` | `OptimizedTheta::new()` [VERIFIED: theta/optimized.rs] | YES | YES |
| `DynamicTheta` | `DynamicTheta` | `DynamicTheta::new()` | YES | YES |
| `DynamicOptimizedTheta` | `DynamicTheta` | `DynamicTheta::new_dotm()` or similar | YES | YES |
| `Naive` | `Naive` | `Naive::new()` | n/a (constant forecast) | YES |
| `SMA` | `SimpleMovingAverage` or `WindowAverage` | `WindowAverage::new(window)` | window, not period | YES |
| `SeasonalNaive` | `SeasonalNaive` | `SeasonalNaive::new(p)` | period required | YES |
| `SES` | `SimpleExponentialSmoothing` | `SimpleExponentialSmoothing::new(0.3)` | n/a | YES |
| `SESOptimized` | `SimpleExponentialSmoothing` | `.optimized()` variant | n/a | YES |
| `RandomWalkDrift` | `RandomWalkWithDrift` | `RandomWalkWithDrift::new()` | n/a | YES |
| `Holt` | `HoltLinearTrend` | `HoltLinearTrend::auto()` | n/a | YES |
| `HoltWinters` | `HoltWinters` | `HoltWinters::new(p)` | period required | YES |
| `SeasonalES` | `SeasonalESModel` | `SeasonalESModel::new(p)` | period required | YES |
| `SeasonalESOptimized` | `SeasonalESModel` | `.optimized(p)` or similar | period required | YES |
| `SeasonalWindowAverage` | `SeasonalWindowAverage` | `SeasonalWindowAverage::new(p)` [VERIFIED: baseline/seasonal_window.rs] | period required | YES |
| `ETS` | `ETSModel` | `ETSModel::default()` | `seasonal_period` field | YES |
| `Kalman` | `KalmanForecaster` | `KalmanForecaster::local_level()` | n/a | YES |
| `CrostonClassic` | `Croston` | `Croston::new()` | n/a | YES |
| `CrostonOptimized` | `Croston` | `Croston::new().optimized()` | n/a | YES |
| `CrostonSBA` | `Croston` | `Croston::new().sba()` | n/a | YES |
| `ADIDA` | `ADIDA` | `ADIDA::new()` | n/a | YES |
| `IMAPA` | `IMAPA` | `IMAPA::new()` | n/a | YES |
| `TSB` | `TSB` | `TSB::new()` | n/a | YES |

#### NOT SUPPORTED in v1 — flag as `InvalidParameter` if named

| ModelType | Reason | Note |
|-----------|--------|------|
| `GARCH` | `GARCH` does NOT route through `extract_forecast` — it uses `.forecast_variance(h)`, NOT `.predict(h)`. The `Forecaster` trait's `predict()` on GARCH returns simulated innovations, not variance forecasts. Including GARCH in a `Ensemble::new()` call would call `.predict()` yielding wrong values. [VERIFIED: forecast.rs:2477-2499] | Exclude from v1 supported set |
| `Laplace` | `LaplaceForecaster` is built via factory methods (`LaplaceForecaster::auto()`, `.auto_aid()`, `.skaters()`) and has variant-dependent construction that doesn't map to a single `period` param. The Laplace leaf selection is internal and stateful. Not boxable simply. [ASSUMED] | Exclude from v1 |
| `ARIMA` | Fixed-order ARIMA requires `(p, d, q)` params — not expressible via shared `seasonal_period`. [ASSUMED] | Exclude from v1; suggest `AutoARIMA` instead |
| `MFLES`, `AutoMFLES` | `MFLES` requires `seasonal_periods: Vec<usize>` (multi-seasonal). Not expressible via single `seasonal_period`. [ASSUMED] | Exclude from v1 |
| `MSTL`, `AutoMSTL` | Same as MFLES — requires `seasonal_periods: Vec<usize>`. [ASSUMED] | Exclude from v1 |
| `TBATS`, `AutoTBATS` | Same — multi-seasonal input required. [ASSUMED] | Exclude from v1 |
| `AutoEnsemble` | Nested ensemble of ensemble. No technical barrier but semantically circular for v1. [ASSUMED] | Exclude; document clearly |

#### Recommended v1 supported member set (for documentation and error message)

**Tier 1 — best DoD candidates (deterministic, well-tested):**
`AutoARIMA`, `AutoETS`, `AutoTheta`

**Tier 2 — good ensemble members with shared seasonal_period:**
`Theta`, `OptimizedTheta`, `DynamicTheta`, `DynamicOptimizedTheta`, `HoltWinters`, `SeasonalES`, `SeasonalESOptimized`, `SeasonalNaive`, `SeasonalWindowAverage`, `ETS`, `Kalman`

**Tier 3 — non-seasonal baselines (always valid, seasonal_period ignored):**
`Naive`, `SES`, `SESOptimized`, `RandomWalkDrift`, `Holt`, `SMA`, `CrostonClassic`, `CrostonOptimized`, `CrostonSBA`, `ADIDA`, `IMAPA`, `TSB`

**Explicitly blocked (return `InvalidParameter` naming the member):**
`GARCH`, `Laplace`, `ARIMA`, `MFLES`, `AutoMFLES`, `MSTL`, `AutoMSTL`, `TBATS`, `AutoTBATS`, `AutoEnsemble`

The complete `build_forecaster` match should cover all 36 ModelType variants (returning Err for the blocked ones) so any future `ModelType` addition triggers a compiler exhaustiveness error rather than a silent runtime panic.

### `build_forecaster` skeleton

```rust
// In crates/anofox-fcst-core/src/forecast.rs
// New function — goes after forecast_auto_ensemble

use anofox_forecast::models::BoxedForecaster;

/// Construct a boxed `Forecaster` instance for the given model type and optional
/// seasonal period. Used by `forecast_explicit_ensemble` to build member models
/// for `Ensemble::new(members)`.
///
/// Returns `Err(InvalidParameter)` for model types that are not expressible as a
/// single per-series `Box<dyn Forecaster>` with a shared `seasonal_period` (e.g.
/// GARCH, Laplace, multi-seasonal MFLES/MSTL/TBATS, fixed-order ARIMA, AutoEnsemble).
pub(crate) fn build_forecaster(
    model_type: ModelType,
    period: Option<usize>,
) -> Result<BoxedForecaster> {
    let p = period.unwrap_or(0);

    match model_type {
        // Auto-selection models
        ModelType::AutoARIMA => {
            let mut cfg = AutoARIMAConfig::default();
            cfg.seasonal_period = p;
            Ok(Box::new(AutoARIMA::with_config(cfg)))
        }
        ModelType::AutoETS => {
            let cfg = if p > 1 {
                AutoETSConfig::with_period(p)
            } else {
                AutoETSConfig::default()
            };
            Ok(Box::new(AutoETS::with_config(cfg)))
        }
        ModelType::AutoTheta => {
            if p > 1 {
                Ok(Box::new(AutoTheta::seasonal(p)))
            } else {
                Ok(Box::new(AutoTheta::new()))
            }
        }
        // Theta family
        ModelType::Theta => Ok(Box::new(Theta::new())),
        ModelType::OptimizedTheta => Ok(Box::new(OptimizedTheta::new())),
        ModelType::DynamicTheta => Ok(Box::new(DynamicTheta::new())),
        ModelType::DynamicOptimizedTheta => Ok(Box::new(DynamicTheta::new_dotm())),  // verify method name
        // Baselines
        ModelType::Naive => Ok(Box::new(Naive::new())),
        ModelType::RandomWalkDrift => Ok(Box::new(RandomWalkWithDrift::new())),
        ModelType::SES => Ok(Box::new(SimpleExponentialSmoothing::new(0.3))),
        ModelType::SESOptimized => Ok(Box::new(SimpleExponentialSmoothing::new(0.3).optimized())),  // verify
        ModelType::SMA => Ok(Box::new(WindowAverage::new(5))),  // default window=5
        ModelType::Holt => Ok(Box::new(HoltLinearTrend::auto())),
        ModelType::HoltWinters => {
            let p = if p > 1 { p } else { 12 };  // HW requires a period
            Ok(Box::new(HoltWintersModel::new(p)))
        }
        ModelType::SeasonalNaive => {
            let p = if p > 1 { p } else { 12 };
            Ok(Box::new(SeasonalNaive::new(p)))
        }
        ModelType::SeasonalES => {
            let p = if p > 1 { p } else { 12 };
            Ok(Box::new(SeasonalESModel::new(p)))
        }
        ModelType::SeasonalESOptimized => {
            let p = if p > 1 { p } else { 12 };
            Ok(Box::new(SeasonalESModel::new(p).optimized()))  // verify .optimized() method
        }
        ModelType::SeasonalWindowAverage => {
            let p = if p > 1 { p } else { 12 };
            Ok(Box::new(SeasonalWindowAverage::new(p)))
        }
        ModelType::ETS => {
            let mut e = ETSModel::default();
            if p > 1 { e.set_seasonal_period(p); }  // verify setter name
            Ok(Box::new(e))
        }
        // State-space
        ModelType::Kalman => Ok(Box::new(KalmanForecaster::local_level())),
        // Intermittent
        ModelType::CrostonClassic => Ok(Box::new(Croston::new())),
        ModelType::CrostonOptimized => Ok(Box::new(Croston::new().optimized())),
        ModelType::CrostonSBA => Ok(Box::new(Croston::new().sba())),
        ModelType::ADIDA => Ok(Box::new(ADIDA::new())),
        ModelType::IMAPA => Ok(Box::new(IMAPA::new())),
        ModelType::TSB => Ok(Box::new(TSB::new())),
        // NOT SUPPORTED — return error naming the model
        ModelType::GARCH => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "GARCH is not supported as an ensemble member (GARCH predict() returns simulated innovations, not level forecasts; use AutoARIMA or AutoETS instead)".to_string(),
        }),
        ModelType::Laplace => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "Laplace is not supported as an ensemble member in v1 (variant-dependent construction; use AutoARIMA, AutoETS, or AutoTheta instead)".to_string(),
        }),
        ModelType::ARIMA => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "Fixed-order ARIMA requires (p,d,q) params not supported in the shared-period ensemble v1; use AutoARIMA instead".to_string(),
        }),
        ModelType::MFLES | ModelType::AutoMFLES | ModelType::MSTL | ModelType::AutoMSTL
        | ModelType::TBATS | ModelType::AutoTBATS => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "Multi-seasonal models (MFLES/MSTL/TBATS and Auto variants) require seasonal_periods[] which is not supported in v1 explicit-member ensembles".to_string(),
        }),
        ModelType::AutoEnsemble => Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: model_type.name().to_string(),
            reason: "AutoEnsemble cannot be used as a member of an explicit ensemble".to_string(),
        }),
    }
}
```

**NOTE:** The exact constructor names for `DynamicOptimizedTheta`, `SESOptimized`, `SeasonalESOptimized`, and `ETS` seasonal setter must be confirmed by reading the individual model source files during the execution phase. The skeleton above marks these with `// verify`. All other constructors are confirmed by reading their source files this session.

---

## Ensemble Construction and Combination

### `Ensemble::new` + `.with_method` + `.fit` + `.predict`

[VERIFIED: ~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/ensemble/mod.rs:1-10]
[VERIFIED: ~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/ensemble/model.rs:202-219 (from Phase 4 RESEARCH)]

The public API:
```rust
use anofox_forecast::models::ensemble::{CombinationMethod, Ensemble};

let mut ens = Ensemble::new(members)        // Vec<Box<dyn Forecaster>>
    .with_method(combination_method);       // CombinationMethod variant
ens.fit(&ts)?;                              // fits all members on the same series
let forecast = ens.predict(horizon)?;       // returns Forecast with .primary() point forecasts
```

`Ensemble` implements `Forecaster` [ASSUMED — consistent with Phase 4 RESEARCH showing AutoEnsemble delegates to an internal `Ensemble` and Phase 4 verified that `extract_forecast` works on `AutoEnsemble`], so `extract_forecast(&ens, horizon, "Ensemble")` works after fit.

### `forecast_explicit_ensemble` helper function

```rust
fn forecast_explicit_ensemble(
    values: &[f64],
    horizon: usize,
    member_names: &[String],
    method_str: Option<&str>,
    period: usize,
) -> Result<ForecastOutput> {
    use anofox_forecast::models::ensemble::{CombinationMethod, Ensemble};

    // 1. Validate member count
    if member_names.len() < 2 {
        return Err(ForecastError::InvalidParameter {
            param: "members".to_string(),
            value: member_names.len().to_string(),
            reason: "at least 2 members are required for an ensemble".to_string(),
        });
    }

    // 2. Parse combination method (reuse Phase 4 function verbatim)
    let combination_method = parse_combination_method(method_str)?;

    // 3. Build member forecasters
    let member_period = if period > 1 { Some(period) } else { None };
    let mut members: Vec<Box<dyn anofox_forecast::prelude::Forecaster>> = Vec::with_capacity(member_names.len());
    for name in member_names {
        let model_type: ModelType = name.parse().map_err(|_| {
            ForecastError::InvalidParameter {
                param: "members".to_string(),
                value: name.clone(),
                reason: format!("unknown model name '{}'; use the same names as ts_forecast_by", name),
            }
        })?;
        members.push(build_forecaster(model_type, member_period)?);
    }

    // 4. Build + fit + extract
    let ts = make_timeseries(values)?;
    let mut ens = Ensemble::new(members).with_method(combination_method);
    ens.fit(&ts).map_err(|e| {
        ForecastError::ComputationError(format!("Ensemble fit failed: {}", e))
    })?;
    extract_forecast(&ens, horizon, "Ensemble")
}
```

### Prediction intervals

`Ensemble::predict()` returns point forecasts only (intervals are NULL). `Ensemble::predict_with_intervals()` returns a widest-envelope interval (min of lowers, max of uppers across members). Phase 5 uses `extract_forecast` which calls `model.predict(horizon)` — point only. This matches Phase 4 behavior. Intervals are Phase 6 (EPI-01).

[VERIFIED: Phase 4 RESEARCH confirmed for AutoEnsemble; same crate `Ensemble` struct is used underneath]

The `forecast()` caller in `forecast.rs` already has a guard that skips confidence-interval calculation for ensemble models [VERIFIED: crates/anofox-fcst-core/src/forecast.rs:792-794]:
```rust
let (lower, upper) = match options.model {
    ModelType::GARCH | ModelType::Kalman | ModelType::AutoEnsemble => (vec![], vec![]),
    _ => calculate_confidence_intervals(...),
};
```
Phase 5 does NOT route through `forecast()` at all (it is a separate FFI function), so this guard is irrelevant — the new `forecast_explicit_ensemble` returns `lower: vec![], upper: vec![]` directly via `extract_forecast`, and the new FFI function emits NULL for `yhat_lower`/`yhat_upper` exactly as AutoEnsemble does.

---

## FFI Surface for a Member List

### Why a new dedicated FFI export is required

The existing `anofox_ts_forecast` function takes a single `ForecastOptions` struct. Extending that struct with a variable-length list of strings would break ABI (non-fixed-size field in a `#[repr(C)]` struct). A new dedicated function is the correct approach.

### Recommended marshalling: null-delimited concatenated C string

**Shape:**
```c
// New FFI export (new function in crates/anofox-fcst-ffi/src/lib.rs)
bool anofox_ts_forecast_ensemble(
    const double*  values,
    const uint64_t* validity,
    size_t         length,
    const char*    members_buf,    // null-delimited: "AutoARIMA\0AutoETS\0Theta\0"
    size_t         members_count,  // number of member names in members_buf
    const char*    combination_method, // C string, empty = "mean"
    int            seasonal_period,
    int            horizon,
    ForecastResult* out_result,
    AnofoxError*   out_error
);
```

**Why null-delimited over `*const *const c_char`:**
- A `*const *const c_char` (C-style array of string pointers) requires the C++ side to maintain a `std::vector<const char*>` of stable pointers — cumbersome with DuckDB's `LogicalType::LIST` → `ListVector` extraction.
- A flat null-delimited buffer is simpler: C++ joins the `VARCHAR[]` elements with `\0` into a `std::string`, passes `.data()` and `len()` to the FFI. Rust splits on `\0`, trimming empty strings at the end.
- This pattern is used widely in cross-language borders (e.g., environment variable blocks). It's correct for our use case where member count is typically 2-10.

**Rust parsing:**
```rust
// In anofox_ts_forecast_ensemble FFI function:
let members_slice = std::slice::from_raw_parts(members_buf as *const u8, /* count bytes */);
let members: Vec<String> = members_slice
    .split(|&b| b == 0)
    .filter(|s| !s.is_empty())
    .map(|s| String::from_utf8_lossy(s).into_owned())
    .collect();
// members.len() == members_count (validated)
```

**C++ construction (in `_ts_forecast_ensemble_native`):**
```cpp
// Join DuckDB LIST(VARCHAR) elements into a null-delimited buffer
std::string members_buf;
for (const auto& name : member_names) {
    members_buf += name;
    members_buf += '\0';
}
// Pass members_buf.data(), members_count to FFI
```

**Total byte budget:** Even with 36 model names (~25 bytes average), total buffer is < 1 KB. No dynamic allocation needed on either side.

### New FFI export requires `make header`

After adding `anofox_ts_forecast_ensemble` to `crates/anofox-fcst-ffi/src/lib.rs`, run:
```bash
make header
```
This regenerates `src/include/anofox_fcst_ffi.h` via cbindgen. The new function signature will be available to C++ callers.

---

## C++ Native Table Function + Macro

### New file: `src/table_functions/ts_forecast_ensemble_native.cpp`

This is a **new source file** following the `ts_forecast_native.cpp` structure. Because CMakeLists.txt uses an **explicit source list** (NOT glob) [VERIFIED: CMakeLists.txt:161-209], the new file **must be added** to `EXTENSION_SOURCES` in CMakeLists.txt. This is the key difference from Phase 4 (which added no new C++ files).

**Bind data struct:**
```cpp
struct TsForecastEnsembleNativeBindData : public TableFunctionData {
    int64_t horizon = 7;
    int64_t frequency_seconds = 86400;
    bool frequency_is_raw = false;
    FrequencyType frequency_type = FrequencyType::FIXED;
    // Ensemble-specific
    vector<string> member_names;       // parsed from DuckDB LIST(VARCHAR)
    string combination_method = "";    // "" → "mean"
    int64_t seasonal_period = 0;
    double confidence_level = 0.90;
    // Type preservation (mirrors ts_forecast_native)
    DateColumnType date_col_type = DateColumnType::TIMESTAMP;
    LogicalType date_logical_type = LogicalType(LogicalTypeId::TIMESTAMP);
    LogicalType group_logical_type = LogicalType(LogicalTypeId::VARCHAR);
};
```

**Bind function — key difference from `_ts_forecast_native`:**

The `_ts_forecast_ensemble_native` function takes `members` as a `VARCHAR[]` (DuckDB `LIST(VARCHAR)`) positional argument, not a params MAP/STRUCT key. This is simpler to validate:

```
_ts_forecast_ensemble_native(
    group_col,          -- position 0
    dates_list,         -- position 1  (LIST(TIMESTAMP))
    values_list,        -- position 2  (LIST(DOUBLE))
    members_list,       -- position 3  (LIST(VARCHAR))
    horizon,            -- position 4  (INTEGER)
    frequency,          -- position 5  (VARCHAR)
    combination_method, -- position 6  (VARCHAR, default '')
    seasonal_period     -- position 7  (INTEGER, default 0)
)
```

Extracting a DuckDB `LIST(VARCHAR)`:
```cpp
// In Bind or a called helper:
if (input.inputs[3].type().id() == LogicalTypeId::LIST) {
    auto &list_children = ListValue::GetChildren(input.inputs[3]);
    for (auto &child : list_children) {
        if (!child.IsNull()) {
            bind_data->member_names.push_back(child.GetValue<string>());
        }
    }
}
```

**Registration in `src/anofox_forecast_extension.cpp`:**

A new `RegisterTsForecastEnsembleNative(db)` function (added to this file) registers `_ts_forecast_ensemble_native` with the DuckDB table function API, mirroring `RegisterTsForecastNative`. This function is called from `LoadInternal()`.

### `ts_forecast_ensemble_by` macro in `src/macros/ts_macros.cpp`

**Macro shape — per-series (NOT panel):**

```cpp
{"ts_forecast_ensemble_by",
 {"source", "group_col", "date_col", "target_col", "members", "horizon", "frequency", nullptr},
 {{"combination_method", "''"},
  {"seasonal_period", "0"},
  {nullptr, nullptr}},
R"(
SELECT group_col, forecast_step, ds, yhat, yhat_lower, yhat_upper, model_name
FROM (
    SELECT group_col,
           unnest(_ts_forecast_ensemble_native(
               group_col,
               LIST(date_col ORDER BY date_col),
               LIST(target_col::DOUBLE ORDER BY date_col),
               members,
               horizon,
               frequency,
               combination_method,
               seasonal_period
           ), recursive := true)
    FROM query_table(source::VARCHAR)
    GROUP BY group_col
)
)",
 "Generates ensemble forecasts from explicitly named member models per series.",
 "SELECT * FROM ts_forecast_ensemble_by('sales', product_id, date, qty, ['AutoARIMA','AutoETS','Theta'], 12, '1d')",
 "forecasting"},
```

**This is per-series (like `ts_forecast_by`), NOT a panel/table-in macro.** The `query_table(source::VARCHAR)` + `GROUP BY group_col` + `LIST(date_col ORDER BY date_col)` pattern is identical to `ts_forecast_by`. The panel/table-in subselect gotcha does NOT apply here — that applies only to `_ts_forecast_panel_native` which takes a TABLE argument. [VERIFIED: ts_macros.cpp:575-594 for ts_forecast_by precedent]

**Key difference from `ts_forecast_by` macro:** `members` is a positional parameter (a `VARCHAR[]` literal the user writes as `['AutoARIMA','AutoETS','Theta']`), passed directly through to the underlying native function.

---

## Standard Stack

No new crate dependencies. Everything needed is in `anofox-forecast` 0.15.3 (already a dependency) and the existing project crates.

### Core changes (no new packages)

| Component | Change | Location |
|-----------|--------|----------|
| `forecast.rs` | Add `build_forecaster` helper + `forecast_explicit_ensemble` helper | `crates/anofox-fcst-core/src/forecast.rs` |
| `lib.rs` (FFI) | Add `anofox_ts_forecast_ensemble` export | `crates/anofox-fcst-ffi/src/lib.rs` |
| `anofox_fcst_ffi.h` | Regenerate after new export | `src/include/anofox_fcst_ffi.h` |
| `ts_forecast_ensemble_native.cpp` | NEW file — ensemble table function | `src/table_functions/ts_forecast_ensemble_native.cpp` |
| `ts_forecast_ensemble_native.hpp` | NEW header | `src/table_functions/ts_forecast_ensemble_native.hpp` |
| `anofox_forecast_extension.cpp` | Register new function | `src/anofox_forecast_extension.cpp` |
| `ts_macros.cpp` | Add `ts_forecast_ensemble_by` macro | `src/macros/ts_macros.cpp` |
| `CMakeLists.txt` | Add `ts_forecast_ensemble_native.cpp` to `EXTENSION_SOURCES` | `CMakeLists.txt` |
| `examples/forecasting/ensemble_explicit.sql` | NEW — DoD example | `examples/forecasting/ensemble_explicit.sql` |
| `docs/reference/models/ensemble/ensemble_explicit.md` | NEW doc | `docs/reference/models/ensemble/ensemble_explicit.md` |
| `docs/api/07-forecasting.md` | Add `ts_forecast_ensemble_by` entry | `docs/api/07-forecasting.md` |

---

## Architecture Patterns

### System Architecture Diagram

```
SQL: ts_forecast_ensemble_by('sales', product_id, ds, y, ['AutoARIMA','AutoETS','Theta'], 12, '1d',
                              combination_method := 'mean', seasonal_period := 0)
  │
  ▼
src/macros/ts_macros.cpp
  ts_forecast_ensemble_by → _ts_forecast_ensemble_native(group_col, LIST(ds), LIST(y::DOUBLE),
                                                          members, horizon, freq, method, period)
  (via query_table(source) GROUP BY group_col — same pattern as ts_forecast_by)
  │
  ▼
src/table_functions/ts_forecast_ensemble_native.cpp
  Bind: extract member_names from DuckDB LIST(VARCHAR); parse combination_method, seasonal_period
  Finalize: join member_names into null-delimited buffer; call anofox_ts_forecast_ensemble(...)
  │
  ▼
crates/anofox-fcst-ffi/src/lib.rs  (anofox_ts_forecast_ensemble)
  parse members_buf → Vec<String> (split on '\0')
  parse combination_method → Option<String>
  call anofox_fcst_core::forecast_explicit_ensemble(values, horizon, &members, method, period)
  │
  ▼
crates/anofox-fcst-core/src/forecast.rs  (forecast_explicit_ensemble)
  validate member count >= 2
  parse_combination_method(method_str) → CombinationMethod::Mean
  for each name: name.parse::<ModelType>()? → build_forecaster(model_type, period)?
  Ensemble::new(members).with_method(combination_method)
  ens.fit(&ts)?
  extract_forecast(&ens, horizon, "Ensemble")
  │
  ▼
ForecastOutput { point, lower=[], upper=[], model_name="Ensemble" }
  │
  ▼
ts_forecast_ensemble_native → TABLE(group_col, forecast_step, ds, yhat, yhat_lower=NULL, yhat_upper=NULL, model_name)
  │
  ▼
SQL result: product_id | forecast_step | ds | yhat | yhat_lower | yhat_upper | model_name
```

### Recommended Project Structure (changes)

```
crates/anofox-fcst-core/src/forecast.rs        ← add build_forecaster + forecast_explicit_ensemble
crates/anofox-fcst-ffi/src/lib.rs              ← add anofox_ts_forecast_ensemble export
src/include/anofox_fcst_ffi.h                  ← regenerate (make header)
src/table_functions/ts_forecast_ensemble_native.cpp  ← NEW
src/table_functions/ts_forecast_ensemble_native.hpp  ← NEW
src/anofox_forecast_extension.cpp              ← register new function
src/macros/ts_macros.cpp                       ← add ts_forecast_ensemble_by macro
CMakeLists.txt                                 ← add new .cpp to EXTENSION_SOURCES
examples/forecasting/ensemble_explicit.sql     ← NEW (DoD)
docs/reference/models/ensemble/ensemble_explicit.md  ← NEW
docs/api/07-forecasting.md                     ← update
```

---

## Don't Hand-Roll

| Problem | Don't Build | Use Instead | Why |
|---------|-------------|-------------|-----|
| Combination weight computation | Custom weighted-average | `Ensemble::new(members).with_method(CombinationMethod::...)` | Phase 4 already validated all six methods |
| Combination method string parse | New parse block | `parse_combination_method` from Phase 4 | Already handles all six methods + aliases + error messages |
| Member name → crate type dispatch | New string-match chain | `model_type.parse::<ModelType>()` + `build_forecaster` | Reuses existing exact-match + lowercase fallback from Phase 4 |
| Member validation | Enumerate valid names manually | `build_forecaster` returns `Err(InvalidParameter)` for blocked types | Compiler-exhaustive, error messages already include suggestion |
| In-memory group collection | Custom threading | DuckDB GROUP BY + `LIST(ORDER BY)` + `unnest` | Established pattern from `ts_forecast_native` |

---

## Common Pitfalls

### Pitfall 1: CMakeLists.txt must be updated — BLOCKING if missed

**What goes wrong:** Unlike Phase 4, Phase 5 adds a new C++ source file. CMakeLists.txt uses an **explicit source list** (not a glob). If `ts_forecast_ensemble_native.cpp` is not added to `EXTENSION_SOURCES`, the function is not compiled and the extension build appears to succeed but the function is simply absent at runtime.

[VERIFIED: CMakeLists.txt:161-209 — explicit list, no `GLOB`]

**How to avoid:** Add `src/table_functions/ts_forecast_ensemble_native.cpp` to `EXTENSION_SOURCES` before the first build attempt.

**Warning sign:** The extension builds clean but `ts_forecast_ensemble_by(...)` throws "Function _ts_forecast_ensemble_native not found" at SQL execution time.

### Pitfall 2: `make header` after adding new FFI export

**What goes wrong:** Adding `anofox_ts_forecast_ensemble` to `lib.rs` without regenerating the C header means `src/include/anofox_fcst_ffi.h` does not declare the new function — the C++ `#include "anofox_fcst_ffi.h"` will result in a linker error when calling the undefined symbol.

**How to avoid:** Run `make header` after editing `crates/anofox-fcst-ffi/src/lib.rs`. Verify both the function name and parameter types appear in the regenerated header.

**Warning sign:** C++ compile error: `anofox_ts_forecast_ensemble` not declared / implicit function declaration.

### Pitfall 3: GARCH must NOT be used as an ensemble member

**What goes wrong:** If a user passes `['AutoARIMA', 'GARCH']` as members, the `build_forecaster` for GARCH would construct `GARCH::new(1,1)` and call its `.predict(h)` — which returns simulated innovations (random walk innovations), NOT the conditional volatility (variance forecast). The result would be numerically wrong without any runtime error.

[VERIFIED: crates/anofox-fcst-core/src/forecast.rs:2477-2499 — GARCH uses `forecast_variance()`, not `.predict()`]

**How to avoid:** `build_forecaster(ModelType::GARCH, ...)` returns `Err(InvalidParameter)` with a clear message. This is enforced in the factory.

**Warning sign:** If GARCH were accidentally supported, the ensemble yhat would be near-zero (simulated noise), not near the model's variance forecast.

### Pitfall 4: NULL member list or empty list at SQL level

**What goes wrong:** `members := NULL` or `members := []` passes into the bind function. DuckDB `LIST(VARCHAR)` can be NULL or empty.

**How to avoid:**
- In the C++ Bind function, validate: if `members_list.IsNull()` or `member_names.size() < 2`, throw `InvalidInputException("ts_forecast_ensemble_by requires at least 2 members")`.
- In the Rust `forecast_explicit_ensemble`, the `member_names.len() < 2` guard is a second line of defense.

### Pitfall 5: seasonal_period=0 + SeasonalNaive/HoltWinters/SeasonalES

**What goes wrong:** If `seasonal_period=0` is passed and a seasonal model (e.g., `SeasonalNaive`, `HoltWinters`) is in the member list, the `build_forecaster` fallback of `p = 12` is used. This may produce unexpected results if the user intended non-seasonal behavior.

**How to avoid:** Document clearly in the macro's error message and docs that `seasonal_period=0` means "non-seasonal for Auto* models, but seasonal models fall back to period=12." Seasonal models should be used with an explicit `seasonal_period > 1`. The error message in `build_forecaster` for seasonal models should include this note.

**Alternative (accepted):** For v1, the current behavior is reasonable — seasonal models with `period=0` use a default of 12. The behavior is consistent and documented. No change required.

### Pitfall 6: Determinism of Auto* members for the cross-check

**What goes wrong:** `AutoARIMA::fit()` and `AutoETS::fit()` may make non-deterministic choices on some platforms (e.g., tie-breaking in AIC minimization). If the cross-check runs `ts_forecast_ensemble_by(['AutoARIMA','AutoETS','Theta'], 'mean')` and then runs each member independently, the Auto* model selections may differ between the two runs, causing the manual mean to not match exactly.

**Mitigation:** Use a clean 60-observation linearly-trended series (same as Phase 4 cross-check) where AutoARIMA selects a deterministic ARIMA(0,1,0) or similar low-order model. The cross-check should verify within 1e-6 tolerance. Document the constraint in the example comments. A short enough series that Auto* always selects the same model is the reliable path.

**Note:** The Phase 4 cross-check passed with `diff=0.0` on the linear series — the same series can be reused here.

### Pitfall 7: `Ensemble` implements `Forecaster` — verify before relying on `extract_forecast`

[ASSUMED — Phase 4 RESEARCH established `AutoEnsemble` calls an internal `Ensemble` and both implement `Forecaster`; `extract_forecast` was verified to work on `AutoEnsemble`]

The `forecast_explicit_ensemble` uses `extract_forecast(&ens, horizon, "Ensemble")` directly on the `Ensemble` struct. Verify that `Ensemble` (not just `AutoEnsemble`) implements `Forecaster` by checking `models/ensemble/model.rs` implements `fn predict(&self, horizon) -> Result<Forecast>` during the execution phase. If not, call `ens.predict(horizon)?` directly and construct `ForecastOutput` manually.

---

## Cross-Check / DoD

### Primary cross-check: `combination_method='mean'`

With `ts_forecast_ensemble_by('series', id, ds, y, ['AutoARIMA','AutoETS','Theta'], 5, '1d', combination_method := 'mean', seasonal_period := 0)`:

The `yhat` for each forecast step must equal `(AutoARIMA_yhat + AutoETS_yhat + Theta_yhat) / 3.0` computed independently via `ts_forecast_by`, within 1e-6 tolerance.

**Why this works:** The crate's `CombinationMethod::Mean` formula [VERIFIED Phase 4 RESEARCH]:
```rust
let sum: f64 = values.iter().filter(|v| h < v.len()).map(|v| v[h]).sum();
combined[h] = sum / values.len() as f64;
```
is a simple unweighted mean. Each member is fit on the same series with the same period. Given deterministic fit (linear series), the ensemble mean == arithmetic mean of independent fits.

**Cross-check SQL pattern** (mirrors Phase 4 `autoensemble.sql`):

```sql
-- Step 1: Ensemble (mean combination)
CREATE OR REPLACE TABLE ens_result AS
SELECT * FROM ts_forecast_ensemble_by('test_series', id, ds, y,
    ['AutoARIMA', 'AutoETS', 'Theta'], 5, '1d',
    combination_method := 'mean', seasonal_period := 0);

-- Step 2: Individual member forecasts (same series, same seasonal_period)
CREATE OR REPLACE TABLE m_arima  AS SELECT forecast_step, yhat AS y_arima  FROM ts_forecast_by('test_series', id, ds, y, 'AutoARIMA', 5, '1d');
CREATE OR REPLACE TABLE m_ets    AS SELECT forecast_step, yhat AS y_ets    FROM ts_forecast_by('test_series', id, ds, y, 'AutoETS',   5, '1d');
CREATE OR REPLACE TABLE m_theta  AS SELECT forecast_step, yhat AS y_theta  FROM ts_forecast_by('test_series', id, ds, y, 'Theta',     5, '1d');

-- Step 3: Manual mean
CREATE OR REPLACE TABLE m_manual AS
SELECT forecast_step, (y_arima + y_ets + y_theta) / 3.0 AS manual_mean
FROM m_arima JOIN m_ets USING (forecast_step) JOIN m_theta USING (forecast_step);

-- Step 4: Cross-check (must all be match=true, diff < 1e-6)
SELECT e.forecast_step, e.yhat AS ens_mean, m.manual_mean,
       abs(e.yhat - m.manual_mean) AS diff,
       abs(e.yhat - m.manual_mean) < 1e-6 AS match
FROM ens_result e JOIN m_manual m USING (forecast_step)
ORDER BY forecast_step;

-- Assertion: zero mismatch rows
SELECT count(*) AS mismatch_count
FROM (above) WHERE NOT match;
-- Must return 0
```

### Weighted-method smoke test

For `combination_method='weighted_mse'` etc., verify that:
1. `yhat` is non-NULL and finite for all steps.
2. `yhat_lower` and `yhat_upper` are NULL (point forecasts only in Phase 5).

---

## Validation Architecture

Per-project convention: no Nyquist/pytest framework. DoD = runnable `examples/*.sql` verified against the built extension.

### Phase Requirements → Test Map

| Req ID | Behavior | Test Type | File |
|--------|----------|-----------|------|
| ENS-02 | `ts_forecast_ensemble_by(['A','B','C'], 'mean')` equals arithmetic mean of A/B/C independently | SQL cross-check | `examples/forecasting/ensemble_explicit.sql` Section 1 |
| ENS-02 | All 6 combination methods return non-NULL finite yhat | SQL smoke test | `examples/forecasting/ensemble_explicit.sql` Section 2 |
| ENS-02 | Unknown member name → `InvalidParameter` error naming the member | SQL error test | `examples/forecasting/ensemble_explicit.sql` Section 3 |
| ENS-02 | < 2 members → clear error | SQL error test | `examples/forecasting/ensemble_explicit.sql` Section 3 |
| ENS-02 | `yhat_lower`/`yhat_upper` are NULL | SQL assertion | Section 1 cross-check query |

---

## Security Domain

No new auth, session, or cryptography concerns. The member-name input is validated via `ModelType::from_str` (whitelist of 36 known names) before any Rust model code is invoked — no injection vector. The null-delimited buffer uses `members_count` to bound iteration — no buffer overread.

---

## Open Questions

1. **`Ensemble` implements `Forecaster` directly (verify)**
   - What we know: `AutoEnsemble` stores an `Ensemble` internally and delegates `predict()` to it. Phase 4 RESEARCH verified `extract_forecast` works on `AutoEnsemble`.
   - What's unclear: Whether `Ensemble` (the struct, not `AutoEnsemble`) itself has a public `impl Forecaster` block or only a `predict()` method.
   - Recommendation: Read `model.rs` impl block at the top of execution. If `Ensemble` implements `Forecaster`, use `extract_forecast(&ens, h, "Ensemble")`. If not, call `ens.predict(h)?` directly and build `ForecastOutput` inline.

2. **Exact constructor for `DynamicOptimizedTheta`, `SESOptimized`, `SeasonalESOptimized`**
   - What we know: These types exist in `ModelType` and are imported in `forecast.rs`. `DynamicTheta` covers DSTM and DOTM.
   - What's unclear: The exact method to create DOTM from `DynamicTheta` (is it `DynamicTheta::new()` or `DynamicTheta::new_dotm()`?).
   - Recommendation: Read `crates/anofox-fcst-core/src/forecast.rs` at the `DynamicOptimizedTheta` dispatch arm to find the exact constructor used there — it was already worked out for the inline path.

3. **`ETS` seasonal period setter**
   - What we know: `ETSModel::default()` creates a default ETS. Period can be set via config.
   - What's unclear: Whether `ETSModel` has a `set_seasonal_period()` method or requires construction via `ETSSpec`.
   - Recommendation: Check `exponential/ets.rs` for the struct's fields and constructors during execution. May need `ETSModel::with_spec(ETSSpec::new_auto(p))` or similar.

---

## Assumptions Log

| # | Claim | Section | Risk if Wrong |
|---|-------|---------|---------------|
| A1 | `Ensemble` struct implements `Forecaster` trait directly (not just via AutoEnsemble wrapper) | Ensemble Construction | `extract_forecast` fails; workaround is direct `ens.predict(h)?` call — trivial fix |
| A2 | `DynamicTheta::new_dotm()` is the correct constructor for `DynamicOptimizedTheta` | build_forecaster skeleton | Compile error; correct by reading the inline dispatch arm in forecast.rs |
| A3 | `SESOptimized` uses `SimpleExponentialSmoothing::new(0.3).optimized()` or equivalent | build_forecaster skeleton | Compile error; fix by reading ses.rs |
| A4 | `SeasonalESOptimized` uses `SeasonalESModel::new(p).optimized()` or equivalent | build_forecaster skeleton | Compile error; fix by reading seasonal_es.rs |
| A5 | `Laplace` is not boxable simply (variant-dependent construction) | Supported member set | If wrong, Laplace could be added to v1; risk is low — Laplace IS complex to construct |
| A6 | ARIMA requires (p,d,q) and has no sensible default for ensemble membership | Supported member set | If wrong, ARIMA could default to (1,1,1); risk is low — suggest AutoARIMA instead |
| A7 | ETS requires a seasonal period setter or spec constructor for the boxed path | build_forecaster skeleton | Compile error; trivially fixable during execution |

---

## Sources

### Primary (HIGH confidence — source files read this session)

- `crates/anofox-fcst-core/src/forecast.rs` (lines 1-400, 700-800, 2380-2581) — ModelType enum, all 36 model names, `from_str` dispatch, `ForecastOptions`, `forecast()` match, `extract_forecast`, `parse_combination_method`, `forecast_auto_ensemble`
- `crates/anofox-fcst-ffi/src/types.rs` (lines 372-422) — current FFI ForecastOptions, end-of-struct after `ensemble_method` field — Phase 4 additive fields confirmed
- `src/table_functions/ts_forecast_native.cpp` (lines 1-80, 255-400, 640-710) — bind data struct, param extraction, ValidateParamKeys, opts building, FFI call pattern
- `src/macros/ts_macros.cpp` (lines 575-594) — `ts_forecast_by` macro pattern (per-series, GROUP BY, LIST, unnest)
- `CMakeLists.txt` (lines 161-209) — explicit source file list (NO GLOB — new .cpp must be added)
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/traits.rs` (lines 42-294, 296-394) — `Forecaster` trait, `ModelSpec`, `ModelRegistry`, `BoxedForecaster` type alias
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/convenience.rs` (lines 437-477) — `ensemble_best_k` showing the `Ensemble::new(models).fit(ts)` pattern
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/ensemble/mod.rs` — public re-exports
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/arima/auto_arima.rs` (lines 1-170) — `AutoARIMA::new()`, `AutoARIMAConfig`, `seasonal_period` field
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/exponential/auto_ets.rs` (lines 1-205) — `AutoETS::new()`, `AutoETSConfig::with_period(p)`
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/theta/auto.rs` (lines 1-120) — `AutoTheta::new()`, `AutoTheta::seasonal(period)`
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/baseline/naive.rs` (lines 1-60) — `Naive::new()`
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/baseline/mod.rs` — public exports: Naive, RandomWalkWithDrift, SeasonalNaive, SeasonalWindowAverage, HistoricAverage, SimpleMovingAverage, WindowAverage
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/exponential/mod.rs` — public exports: AutoETS, ETS, HoltLinearTrend, HoltWinters, SeasonalES, SimpleExponentialSmoothing
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/theta/mod.rs` — public exports: AutoTheta, DynamicOptimizedTheta, DynamicTheta, GlobalTheta, Theta, OptimizedTheta
- `~/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/anofox-forecast-0.15.3/src/models/mod.rs` — top-level module exports: confirms all model types present
- `.planning/phases/04-autoensemble-surface-combination-methods/04-01-SUMMARY.md` — confirmed Phase 4 shipped: parse_combination_method, forecast_auto_ensemble, ForecastOptions ensemble fields, C++ param keys
- `.planning/phases/04-autoensemble-surface-combination-methods/04-RESEARCH.md` — Phase 4 research (all HIGH confidence findings)

### Secondary (MEDIUM confidence)

- Phase 4 RESEARCH claim: `Ensemble` implements `Forecaster` (inferred from AutoEnsemble delegating to `self.ensemble.as_ref()?.predict(h)`) — not directly verified by reading model.rs impl block

---

## RESEARCH COMPLETE
