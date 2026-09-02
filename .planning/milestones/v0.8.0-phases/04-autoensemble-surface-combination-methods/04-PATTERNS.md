# Phase 4: AutoEnsemble Surface + Combination Methods - Pattern Map

**Mapped:** 2026-08-30
**Files analyzed:** 8 files modified + 2 files created
**Analogs found:** 8 / 8 (all existing files use the GARCH/Kalman additive-field pattern as primary analog; new files use existing examples/docs as analog)

---

## File Classification

| New/Modified File | Role | Data Flow | Closest Analog | Match Quality |
|-------------------|------|-----------|----------------|---------------|
| `crates/anofox-fcst-core/src/forecast.rs` | model/dispatch | request-response | itself (Kalman arm, lines 739–743, 2453–2464) | exact — add enum variant + helper |
| `crates/anofox-fcst-ffi/src/types.rs` | FFI struct | request-response | itself (garch_p/garch_q/kalman_model fields, lines 407–413, 568–572) | exact — additive append |
| `crates/anofox-fcst-ffi/src/lib.rs` | FFI dispatch | request-response | itself (kalman_model parse block, lines 3452–3476; build_core_options, lines 4153–4176) | exact — add parse + thread |
| `src/scalar_functions/ts_forecast_scalar.cpp` | scalar function | request-response | itself (garch_p/garch_q/kalman_model blocks, lines 51–53, 128–132, 429–431, 446–448, 482–488) | exact — 5-site additive update |
| `src/table_functions/ts_forecast_native.cpp` | table function | request-response | itself (garch_p/garch_q/kalman_model blocks, lines 50–53, 275–278, 360–363, 660–667) | exact — 4-site additive update |
| `src/macros/ts_macros.cpp` | SQL macro | request-response | itself (ts_forecast_by, lines 575–594) | exact — NO CHANGES NEEDED |
| `examples/autoensemble.sql` | example/test | batch | `examples/` existing .sql files | role-match |
| `docs/reference/models/ensemble/autoensemble.md` | docs | — | existing model docs under `docs/reference/models/` | role-match |

---

## Pattern Assignments

### `crates/anofox-fcst-core/src/forecast.rs` (model dispatch, request-response)

**Analog:** itself — Kalman additive arm

**Step 1 — ModelType enum variant** (lines 149–156, after `Kalman`):
```rust
    // Classical Models (2) — Phase 3 additions
    GARCH,
    Kalman,
    // ↑ ADD AutoEnsemble HERE (Phase 4)
```

Copy pattern:
```rust
    /// AutoEnsemble: auto-fits AutoARIMA/AutoETS/AutoTheta, ranks by in-sample MSE,
    /// combines top-K members using the specified CombinationMethod.
    AutoEnsemble,
```

**Step 2 — `from_str` exact-match arm** (line 209, after `"Kalman"` arm):
```rust
            "GARCH" => return Ok(ModelType::GARCH),
            "Kalman" => return Ok(ModelType::Kalman),
            // ADD:
            "AutoEnsemble" => return Ok(ModelType::AutoEnsemble),
```

Case-insensitive fallback block (after `"kalman"` arm in the lowercase match):
```rust
            "autoensemble" | "auto_ensemble" => Ok(ModelType::AutoEnsemble),
```

**Step 3 — `name()` method** (line 324, after `ModelType::Kalman => "Kalman"`):
```rust
            ModelType::GARCH => "GARCH",
            ModelType::Kalman => "Kalman",
            // ADD:
            ModelType::AutoEnsemble => "AutoEnsemble",
```

**Step 4 — `ForecastOptions` struct** (lines 368–373, after `kalman_model`):
```rust
    /// GARCH p order (0 = use default 1). Only consulted when model is GARCH.
    pub garch_p: usize,
    /// GARCH q order (0 = use default 1). Only consulted when model is GARCH.
    pub garch_q: usize,
    /// Kalman state-space spec. None = "local_level". Only consulted when model is Kalman.
    pub kalman_model: Option<String>,
    // ADD AFTER kalman_model:
    /// AutoEnsemble: number of top models to select (0 → default 3).
    pub ensemble_top_k: usize,
    /// AutoEnsemble: combination method string. None → "mean" (Phase 4 default).
    pub ensemble_method: Option<String>,
```

**Step 5 — `Default` impl** (lines 391–395, after `kalman_model: None`):
```rust
            garch_p: 0,
            garch_q: 0,
            kalman_model: None,
            // ADD:
            ensemble_top_k: 0,
            ensemble_method: None,
```

**Step 6 — `forecast()` match arm** (lines 739–743, after `ModelType::Kalman` arm):
```rust
        ModelType::Kalman => forecast_kalman(
            &clean_values,
            options.horizon,
            options.kalman_model.as_deref(),
        ),
        // ADD:
        ModelType::AutoEnsemble => forecast_auto_ensemble(
            &clean_values,
            options.horizon,
            if options.ensemble_top_k == 0 { 3 } else { options.ensemble_top_k },
            options.ensemble_method.as_deref(),
            period,
        ),
```

**Step 7 — confidence-interval skip** (lines 750–752):
```rust
        let (lower, upper) = match options.model {
            ModelType::GARCH | ModelType::Kalman => (vec![], vec![]),
            // AutoEnsemble: point forecasts only in Phase 4; intervals deferred to Phase 6 (EPI-01)
            ModelType::AutoEnsemble => (vec![], vec![]),
            _ => calculate_confidence_intervals(&result.point, &clean_values, options.confidence_level),
        };
```

**Step 8 — `forecast_auto_ensemble()` helper** (add after `forecast_kalman`, around line 2465):

Mirror of `forecast_kalman` (lines 2453–2464):
```rust
fn forecast_kalman(values: &[f64], horizon: usize, spec: Option<&str>) -> Result<ForecastOutput> {
    let ts = make_timeseries(values)?;
    let mut model = match spec.unwrap_or("local_level") {
        "local_linear_trend" => KalmanForecaster::local_linear_trend(),
        _ => KalmanForecaster::local_level(),
    };
    model
        .fit(&ts)
        .map_err(|e| ForecastError::ComputationError(format!("Kalman fit failed: {}", e)))?;
    extract_forecast(&model, horizon, "Kalman")
}
```

New helper to write:
```rust
fn forecast_auto_ensemble(
    values: &[f64],
    horizon: usize,
    top_k: usize,
    method_str: Option<&str>,
    period: usize,
) -> Result<ForecastOutput> {
    use anofox_forecast::models::ensemble::{AutoEnsemble, AutoEnsembleConfig};
    use anofox_forecast::models::ensemble::model::CombinationMethod;

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

fn parse_combination_method(s: Option<&str>) -> Result<CombinationMethod> {
    use anofox_forecast::models::ensemble::model::CombinationMethod;
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

Note: `From<ForecastOptions> for ForecastOptionsExog` (around line 530–551) must also propagate the two new fields — add `ensemble_top_k` and `ensemble_method` passthrough matching every other field in that impl.

---

### `crates/anofox-fcst-ffi/src/types.rs` (FFI struct, request-response)

**Analog:** itself — garch_p/garch_q/kalman_model additive fields

**ForecastOptions** (lines 406–413, current last three fields):
```rust
    /// GARCH p order (0 → default 1). Only consulted when model is "GARCH".
    pub garch_p: c_int,          // line 407
    /// GARCH q order (0 → default 1). Only consulted when model is "GARCH".
    pub garch_q: c_int,          // line 409
    /// Kalman state-space spec. Empty string = "local_level" (default).
    /// Accepted values: "" | "local_level" | "local_linear_trend".
    /// Only consulted when model is "Kalman".
    pub kalman_model: [c_char; 32],  // lines 411–413
}
```

Append after `kalman_model` — additive only, do NOT reorder:
```rust
    /// AutoEnsemble: number of top models to select (0 → default 3).
    /// Only consulted when model is "AutoEnsemble".
    pub ensemble_top_k: c_int,
    /// AutoEnsemble: combination method string.
    /// Accepted: "" | "mean" | "median" | "weighted_mse" | "inverse_aic" | "stacking" | "horizon_adaptive"
    /// (and common aliases). Empty = "mean" (Phase 4 default).
    /// Only consulted when model is "AutoEnsemble".
    pub ensemble_method: [c_char; 32],
```

**ForecastOptionsExog** (lines 567–572, current last three fields):
```rust
    /// GARCH p order (0 → default 1). Only consulted when model is "GARCH".
    pub garch_p: c_int,
    /// GARCH q order (0 → default 1). Only consulted when model is "GARCH".
    pub garch_q: c_int,
    /// Kalman state-space spec. Empty string = "local_level" (default).
    pub kalman_model: [c_char; 32],
}
```

Same two fields appended after `kalman_model` — identical to `ForecastOptions` additions.

**ABI safety rule:** Always append at the END. Inserting in the middle shifts offsets and silently corrupts GARCH/Kalman callers. Zero-init via `memset` handles defaults automatically (0 → `ensemble_top_k` default 3; NUL string → `ensemble_method` default "mean").

---

### `crates/anofox-fcst-ffi/src/lib.rs` (FFI dispatch, request-response)

**Analog:** itself — kalman_model parse pattern (lines 3452–3476) and `build_core_options` (lines 4118–4177)

**Location A: `anofox_ts_forecast` function (~line 3452–3476)** — add after existing kalman_model block:

Existing pattern to mirror:
```rust
        // Parse kalman_model spec (empty → None → local_level default)
        let kalman_model = CStr::from_ptr(opts.kalman_model.as_ptr())
            .to_str()
            .ok()
            .filter(|s| !s.is_empty())
            .map(str::to_owned);

        let core_opts = anofox_fcst_core::ForecastOptions {
            model: model_type,
            // ... existing fields ...
            garch_p: opts.garch_p as usize,
            garch_q: opts.garch_q as usize,
            kalman_model,
        };
```

Add after `kalman_model` parse, before `core_opts`:
```rust
        // Parse ensemble_method (empty → None → "mean" default in core)
        let ensemble_method = CStr::from_ptr(opts.ensemble_method.as_ptr())
            .to_str()
            .ok()
            .filter(|s| !s.is_empty())
            .map(str::to_owned);
```

Add to `core_opts` struct literal (after `kalman_model`):
```rust
            kalman_model,
            ensemble_top_k: opts.ensemble_top_k as usize,
            ensemble_method,
```

**Location B: `build_core_options` (~lines 4153–4177)** — identical additions:
```rust
    // Parse kalman_model spec (empty → None → local_level default)
    let kalman_model = CStr::from_ptr(opts.kalman_model.as_ptr())
        .to_str()
        .ok()
        .filter(|s| !s.is_empty())
        .map(str::to_owned);
    // ADD:
    let ensemble_method = CStr::from_ptr(opts.ensemble_method.as_ptr())
        .to_str()
        .ok()
        .filter(|s| !s.is_empty())
        .map(str::to_owned);

    Ok(anofox_fcst_core::ForecastOptions {
        // ... existing fields ...
        kalman_model,
        // ADD:
        ensemble_top_k: opts.ensemble_top_k as usize,
        ensemble_method,
    })
```

**Post-edit:** Run `make header` to regenerate `anofox_fcst_ffi.h` via cbindgen. The C++ files include this header; the new fields are automatically available in C++ after regeneration.

---

### `src/scalar_functions/ts_forecast_scalar.cpp` (scalar function, request-response)

**Analog:** itself — 5 GARCH/Kalman sites

**Site 1 — `TsForecastScalarBindData` struct** (lines 51–53, after `kalman_model`):
```cpp
    int64_t garch_p = 0;
    int64_t garch_q = 0;
    string kalman_model = "";
    // ADD:
    int64_t ensemble_top_k = 0;
    string ensemble_method = "";
```

Also add to `Copy()` method (lines 72–74, after `kalman_model`):
```cpp
        copy->garch_p = garch_p;
        copy->garch_q = garch_q;
        copy->kalman_model = kalman_model;
        // ADD:
        copy->ensemble_top_k = ensemble_top_k;
        copy->ensemble_method = ensemble_method;
```

**Site 2 — `ValidateParams` valid_keys set** (lines 128–132):
```cpp
    static const unordered_set<string> valid_keys = {
        "model", "seasonal_period", "seasonal_periods", "confidence_level", "window", "model_pool",
        "laplace_variant", "laplace_seasonal_batch_init",
        "garch_p", "garch_q", "kalman_model"
        // ADD:
        , "top_k", "combination_method"   // Phase 4: AutoEnsemble params
    };
```

Also update the error message string in `throw InvalidInputException(...)` (line 162) to append `top_k, combination_method`.

**Site 3 — Local variable declarations in `TsForecastScalarExecute`** (lines 429–431, after `kalman_model`):
```cpp
        int64_t garch_p = bind_data.garch_p;
        int64_t garch_q = bind_data.garch_q;
        string kalman_model = bind_data.kalman_model;
        // ADD:
        int64_t ensemble_top_k = bind_data.ensemble_top_k;
        string ensemble_method = bind_data.ensemble_method;
```

**Site 4 — MAP/STRUCT params parsing block** (lines 446–448, after `kalman_model`):
```cpp
            garch_p = ParseInt64Param(params_val, "garch_p", 0);
            garch_q = ParseInt64Param(params_val, "garch_q", 0);
            kalman_model = ParseStringParam(params_val, "kalman_model", "");
            // ADD:
            ensemble_top_k = ParseInt64Param(params_val, "top_k", 0);
            ensemble_method = ParseStringParam(params_val, "combination_method", "");
```

**Site 5 — ForecastOptions opts building** (lines 482–488, after `kalman_model` strncpy block):
```cpp
        opts.garch_p = static_cast<int>(garch_p);
        opts.garch_q = static_cast<int>(garch_q);
        if (!kalman_model.empty()) {
            strncpy(opts.kalman_model, kalman_model.c_str(),
                    sizeof(opts.kalman_model) - 1);
            opts.kalman_model[sizeof(opts.kalman_model) - 1] = '\0';
        }
        // ADD:
        opts.ensemble_top_k = static_cast<int>(ensemble_top_k);
        if (!ensemble_method.empty()) {
            strncpy(opts.ensemble_method, ensemble_method.c_str(),
                    sizeof(opts.ensemble_method) - 1);
            opts.ensemble_method[sizeof(opts.ensemble_method) - 1] = '\0';
        }
```

---

### `src/table_functions/ts_forecast_native.cpp` (table function, request-response)

**Analog:** itself — 4 GARCH/Kalman sites

**Site 1 — `TsForecastNativeBindData` struct** (lines 50–53, after `kalman_model`):
```cpp
    // Classical model params (Phase 3)
    int64_t garch_p = 0;
    int64_t garch_q = 0;
    string kalman_model = "";
    // ADD (Phase 4):
    int64_t ensemble_top_k = 0;
    string ensemble_method = "";
```

**Site 2 — `ValidateParamKeys` valid_keys set** (lines 275–278):
```cpp
    static const unordered_set<string> valid_keys = {
        "model", "seasonal_period", "seasonal_periods", "confidence_level", "window", "model_pool",
        "laplace_variant", "laplace_seasonal_batch_init",
        "garch_p", "garch_q", "kalman_model"
        // ADD:
        , "top_k", "combination_method"   // Phase 4: AutoEnsemble params
    };
```

Also update the error message string in `throw InvalidInputException(...)` (line 308) to append `top_k, combination_method`.

**Site 3 — Bind function params parsing** (lines 360–363, after `kalman_model`):
```cpp
        // Classical model params (Phase 3)
        bind_data->garch_p = ParseInt64FromParams(params, "garch_p", 0);
        bind_data->garch_q = ParseInt64FromParams(params, "garch_q", 0);
        bind_data->kalman_model = ParseStringFromParams(params, "kalman_model", "");
        // ADD:
        bind_data->ensemble_top_k = ParseInt64FromParams(params, "top_k", 0);
        bind_data->ensemble_method = ParseStringFromParams(params, "combination_method", "");
```

**Site 4 — opts building in Finalize/Execute** (lines 660–667, after `kalman_model` strncpy block):
```cpp
            // Classical model params (Phase 3)
            opts.garch_p = static_cast<int>(bind_data.garch_p);
            opts.garch_q = static_cast<int>(bind_data.garch_q);
            if (!bind_data.kalman_model.empty()) {
                strncpy(opts.kalman_model, bind_data.kalman_model.c_str(),
                        sizeof(opts.kalman_model) - 1);
                opts.kalman_model[sizeof(opts.kalman_model) - 1] = '\0';
            }
            // ADD:
            opts.ensemble_top_k = static_cast<int>(bind_data.ensemble_top_k);
            if (!bind_data.ensemble_method.empty()) {
                strncpy(opts.ensemble_method, bind_data.ensemble_method.c_str(),
                        sizeof(opts.ensemble_method) - 1);
                opts.ensemble_method[sizeof(opts.ensemble_method) - 1] = '\0';
            }
```

---

### `src/macros/ts_macros.cpp` (SQL macro) — NO CHANGES REQUIRED

The `ts_forecast_by` macro (lines 575–594) passes `params` MAP/STRUCT through verbatim to `_ts_forecast_scalar`. Adding `top_k` and `combination_method` to the validated key sets inside the scalar and native functions is sufficient. Confirmed by RESEARCH.md (lines 462–466).

---

### `examples/autoensemble.sql` (NEW — example/test)

**Analog:** existing `examples/*.sql` files (role-match)

Structure to follow — per DoD, the file must contain:

1. Synthetic 60-observation series creation (`generate_series`).
2. AutoEnsemble Mean forecast (top_k=3).
3. Individual AutoARIMA / AutoETS / AutoTheta forecasts (same seasonal_period=0).
4. Manual arithmetic mean cross-check with `abs(ensemble - manual) < 1e-6`.
5. Six-method smoke test — one block per `combination_method` variant, each must return non-NULL finite `yhat`.

Key SQL pattern (from RESEARCH.md lines 689–717):
```sql
CREATE OR REPLACE TABLE ae_test AS
SELECT i AS id, '2020-01-01'::DATE + INTERVAL (i-1) DAY AS ds, 10.0 + i * 0.5 AS y
FROM generate_series(1, 60) t(i);

SELECT * FROM ts_forecast_by('ae_test', id, ds, y, 'AutoEnsemble', 5, '1d',
    {top_k: 3, combination_method: 'mean', seasonal_period: 0});
```

---

### `docs/reference/models/ensemble/autoensemble.md` (NEW — docs)

**Analog:** existing model docs under `docs/reference/models/` (role-match)

Must document: method string `'AutoEnsemble'`, params `top_k` (default 3) and `combination_method` (default `'mean'`), all six accepted `combination_method` strings with aliases, note that `yhat_lower`/`yhat_upper` are NULL in Phase 4, note that `seasonal_period` is shared with other methods.

---

## Shared Patterns

### Additive-ABI Extension Pattern
**Source:** `crates/anofox-fcst-ffi/src/types.rs` lines 406–413 (garch_p/garch_q/kalman_model)
**Apply to:** `ForecastOptions` and `ForecastOptionsExog` in `types.rs`

Rule: New fields go at the END of the `#[repr(C)]` struct. Zero-init via `memset(&opts, 0, sizeof(opts))` at C++ callsites handles all defaults. Verify both `ForecastOptions` and `ForecastOptionsExog` receive identical additions.

### String→c_char Array Copy Pattern
**Source:** `src/scalar_functions/ts_forecast_scalar.cpp` lines 484–488; `src/table_functions/ts_forecast_native.cpp` lines 663–667
**Apply to:** `ensemble_method` writes in both C++ files

```cpp
if (!kalman_model.empty()) {
    strncpy(opts.kalman_model, kalman_model.c_str(),
            sizeof(opts.kalman_model) - 1);
    opts.kalman_model[sizeof(opts.kalman_model) - 1] = '\0';
}
```

Use `if (!field.empty())` guard — zero-init already wrote NUL bytes; only write when non-empty.

### CStr→Option\<String\> Parse Pattern
**Source:** `crates/anofox-fcst-ffi/src/lib.rs` lines 3452–3457 (kalman_model)
**Apply to:** `ensemble_method` parse in `anofox_ts_forecast` and `build_core_options`

```rust
let kalman_model = CStr::from_ptr(opts.kalman_model.as_ptr())
    .to_str()
    .ok()
    .filter(|s| !s.is_empty())
    .map(str::to_owned);
```

### `extract_forecast` Trait-Based Model Pattern
**Source:** `crates/anofox-fcst-core/src/forecast.rs` lines 2453–2464 (`forecast_kalman`)
**Apply to:** `forecast_auto_ensemble()` helper

`AutoEnsemble` implements `Forecaster` — call `model.fit(&ts)`, then `extract_forecast(&model, horizon, "AutoEnsemble")`. Do not use the older `calculate_fitted_values` path.

### Error Handling in Forecast Helpers
**Source:** `crates/anofox-fcst-core/src/forecast.rs` line 2461
**Apply to:** `forecast_auto_ensemble` fit call

```rust
model
    .fit(&ts)
    .map_err(|e| ForecastError::ComputationError(format!("Kalman fit failed: {}", e)))?;
```

Mirror: `.map_err(|e| ForecastError::ComputationError(format!("AutoEnsemble fit failed: {}", e)))?`

`ConvergenceFailure` from `fit` propagates as `ComputationError` through this path, then is caught at the FFI boundary and emitted as a DuckDB `InvalidInputException` — no special handling needed.

---

## No Analog Found

None. All files are in-place edits of existing well-patterned files. The two new files (`examples/autoensemble.sql`, `docs/...`) have role-match analogs in the existing `examples/` and `docs/` directories.

---

## Post-Edit Actions Required

| Action | Trigger | Command |
|--------|---------|---------|
| Regenerate C header | After editing `crates/anofox-fcst-ffi/src/types.rs` | `make header` |
| Build and load extension | After all edits | `make rust` + DuckDB load |
| Run cross-check example | After build | `duckdb < examples/autoensemble.sql` |

---

## Metadata

**Analog search scope:** `crates/anofox-fcst-core/src/`, `crates/anofox-fcst-ffi/src/`, `src/scalar_functions/`, `src/table_functions/`, `src/macros/`
**Files scanned:** 5 source files (all read in full for the relevant sections)
**Pattern extraction date:** 2026-08-30
