---
phase: 04-autoensemble-surface-combination-methods
plan: "01"
subsystem: forecasting
tags:
  - ensemble
  - autoensemble
  - combination-methods
  - rust-ffi
  - sql-surface

dependency_graph:
  requires:
    - "Phase 3 (Classical models: GARCH/Kalman additive-field pattern)"
  provides:
    - "ModelType::AutoEnsemble end-to-end SQL surface"
    - "parse_combination_method + forecast_auto_ensemble helpers"
    - "ensemble_top_k + ensemble_method FFI fields on both structs"
    - "top_k / combination_method C++ param keys on both dispatch paths"
  affects:
    - "Phase 5 (ENS-02 — explicit member ensembles build on this surface)"
    - "Phase 6 (EPI-01 — prediction intervals, INSP-01 — member introspection)"

tech_stack:
  added:
    - "AutoEnsemble / AutoEnsembleConfig / CombinationMethod from anofox-forecast 0.15.3"
    - "parse_combination_method (all 6 methods + aliases; custom rejected)"
    - "forecast_auto_ensemble helper (mirrors forecast_kalman pattern)"
  patterns:
    - "Additive-ABI FFI struct extension (append at END — never middle-insert)"
    - "CStr→Option<String> parse pattern for ensemble_method"
    - "Guarded strncpy for c_char array fields in C++"
    - "extract_forecast trait-based helper for Forecaster implementations"

key_files:
  created:
    - "examples/forecasting/autoensemble.sql"
  modified:
    - "crates/anofox-fcst-core/src/forecast.rs"
    - "crates/anofox-fcst-ffi/src/types.rs"
    - "crates/anofox-fcst-ffi/src/lib.rs"
    - "src/scalar_functions/ts_forecast_scalar.cpp"
    - "src/table_functions/ts_forecast_native.cpp"
    - "src/include/anofox_fcst_ffi.h (regenerated via make header)"

decisions:
  - "CombinationMethod import: use anofox_forecast::models::ensemble::CombinationMethod (re-exported); model submodule is private"
  - "Default combination_method: empty string maps to Mean (overrides crate WeightedMSE default)"
  - "forecast_with_model dispatch: AutoEnsemble uses fixed top_k=3, None method_str (defaults to Mean)"
  - "model_name output: plain AutoEnsemble (not AutoEnsemble(Mean)); combination visible in Phase 6"
  - "CI-skip guard: AutoEnsemble joined to GARCH|Kalman pattern in both forecast() and forecast_with_exog()"

metrics:
  duration: "9m 31s"
  completed: "2026-08-30T20:51:32Z"
  tasks_completed: 3
  tasks_total: 3
  commits: 3
  files_changed: 7
  insertions: 430
  deletions: 11

status: complete

actuals:
  tokens: 32000
  tasks: 3
  commits: 3
---

# Phase 04 Plan 01: AutoEnsemble End-to-End Tracer Slice Summary

AutoEnsemble model surface wired Rust-FFI-C++-SQL, with Mean combination cross-check proving `abs(ensemble_yhat - AVG(AutoARIMA, AutoETS, AutoTheta)) = 0.0` on a 60-obs series.

## Tasks Completed

| Task | Name | Commit | Key Files |
|------|------|--------|-----------|
| 1 | Wire ModelType::AutoEnsemble through Rust core | 15cf981 | `crates/anofox-fcst-core/src/forecast.rs` |
| 2 | Extend FFI structs, thread fields, regen header | 6bf9213 | `crates/anofox-fcst-ffi/src/types.rs`, `lib.rs`, `anofox_fcst_ffi.h` |
| 3 | C++ param keys, extension build, cross-check example | 6b7d31a | `ts_forecast_scalar.cpp`, `ts_forecast_native.cpp`, `autoensemble.sql` |

## What Was Built

The full vertical tracer slice for `method := 'AutoEnsemble'` through the established GARCH/Kalman additive-field precedent:

**Rust core (`forecast.rs`):**
- `ModelType::AutoEnsemble` variant with `from_str` exact-match ("AutoEnsemble") and lowercase fallback ("autoensemble", "auto_ensemble"), `name()`, and dispatch arms in `forecast()` and `forecast_with_model()`
- Two new struct fields added additively (after `kalman_model`) on both `ForecastOptions` and `ForecastOptionsExog`: `ensemble_top_k: usize` (0 → default 3) and `ensemble_method: Option<String>` (None → Mean)
- `From<ForecastOptions> for ForecastOptionsExog` updated to propagate both new fields
- AutoEnsemble joined to both CI-skip guards and the `calculate_fitted_values` guard (point-forecast-only in Phase 4; intervals deferred to EPI-01)
- `parse_combination_method(s: Option<&str>)` — maps all 6 combination method strings plus aliases; empty string → Mean; "custom" explicitly rejected (ENS-F1 deferred)
- `forecast_auto_ensemble(values, horizon, top_k, method_str, period)` — mirrors `forecast_kalman` pattern; calls `AutoEnsemble::with_config(AutoEnsembleConfig { top_k, combination_method, seasonal_period })`; uses `extract_forecast(&model, horizon, "AutoEnsemble")`

**Rust FFI (`types.rs`, `lib.rs`):**
- `ensemble_top_k: c_int` and `ensemble_method: [c_char; 32]` appended at END of both `ForecastOptions` (FFI) and `ForecastOptionsExog` (FFI) — additive-ABI safe; zero-init via `memset` at C++ callsites handles defaults
- All three `lib.rs` core-option construction sites thread `ensemble_top_k as usize` and `ensemble_method` (CStr→Option<String> parse matching `kalman_model` pattern)

**C header (`anofox_fcst_ffi.h`):**
- Regenerated via `make header` (cbindgen); both structs expose `ensemble_top_k` and `ensemble_method` to C++ callers (2 occurrences each)

**C++ (`ts_forecast_scalar.cpp`, `ts_forecast_native.cpp`):**
- Both bind-data structs gain `ensemble_top_k: int64_t` and `ensemble_method: string`
- `Copy()` method updated (scalar only)
- `valid_keys` sets gain `"top_k"` and `"combination_method"` on both functions
- Error messages updated to list `top_k, combination_method`
- Params parse: `ParseInt64Param/FromParams("top_k")` and `ParseStringParam/FromParams("combination_method")`
- opts building: `opts.ensemble_top_k = static_cast<int>(...)` + guarded `strncpy` for `opts.ensemble_method`

**Example (`examples/forecasting/autoensemble.sql`):**
- 3 sections: (1) Mean cross-check DoD proof, (2) basic usage, (3) six-method smoke test
- All 5 cross-check rows: `match=true`, `diff=0.0` — Mean combination == arithmetic mean of AutoARIMA+AutoETS+AutoTheta within 1e-6
- Assertion query returns 0 rows (cross-check passes)
- `yhat_lower`/`yhat_upper` are NULL (point forecasts only in Phase 4)
- All 6 combination methods (mean, median, weighted_mse, inverse_aic, stacking, horizon_adaptive) produce finite non-NULL `yhat`

## Verification Results

```
Acceptance criteria:
  [x] cargo build -p anofox-fcst-core — clean
  [x] grep -c 'ModelType::AutoEnsemble' forecast.rs — 8 (>= 4)
  [x] fn forecast_auto_ensemble — exists once
  [x] fn parse_combination_method — exists once
  [x] pub ensemble_top_k: usize — 2 occurrences (both core structs)
  [x] pub ensemble_method: Option<String> — 2 occurrences (both core structs)
  [x] From<ForecastOptions> for ForecastOptionsExog propagates both fields
  [x] Kalman tests: 2 passed, 0 failed

  [x] cargo build -p anofox-fcst-ffi — clean
  [x] pub ensemble_top_k: c_int — 2 (both FFI structs)
  [x] pub ensemble_method: [c_char; 32] — 2 (both FFI structs)
  [x] ensemble_top_k in anofox_fcst_ffi.h — 2 occurrences
  [x] ensemble_method in anofox_fcst_ffi.h — 2 occurrences
  [x] opts.ensemble_top_k as usize — 3 (three lib.rs construction sites)

  [x] Extension builds clean (ninja build release — no errors, only pre-existing GCC warnings)
  [x] autoensemble.sql runs clean against built extension
  [x] Cross-check: all 5 steps match=true, diff=0.0 (tolerance 1e-6 — exact)
  [x] yhat_lower / yhat_upper NULL for AutoEnsemble
  [x] Assertion query: 0 rows (no mismatch)
  [x] All six combination_method variants return finite yhat (Section 3 smoke test)
```

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Import Path] CombinationMethod module is private**
- **Found during:** Task 1 first build attempt
- **Issue:** `anofox_forecast::models::ensemble::model` is `mod model` (private); the plan's suggested import `use anofox_forecast::models::ensemble::model::CombinationMethod` failed with E0603
- **Fix:** Used the public re-export: `use anofox_forecast::models::ensemble::CombinationMethod` (verified by reading `mod.rs`). The function signature was also corrected to use the full public path `anofox_forecast::models::ensemble::CombinationMethod` in the return type, and changed to just `CombinationMethod` inside the function body via the local `use`.
- **Files modified:** `crates/anofox-fcst-core/src/forecast.rs`
- **Commit:** 15cf981 (same commit as the feature, fixed before committing)

No other deviations — plan executed as written. All 8 forecast.rs edits, all 4 types.rs edits, all 3 lib.rs threading sites, all 6 scalar + 4 native C++ sites matched the plan exactly.

## Known Stubs

None. AutoEnsemble is fully wired end-to-end with a passing DoD cross-check.

The following are explicitly deferred per plan scope (not stubs):
- Prediction intervals (`yhat_lower`/`yhat_upper`) — Phase 6 (EPI-01)
- Member/weight introspection — Phase 6 (INSP-01)
- Custom combination method (`CombinationMethod::Custom`) — ENS-F1, deferred beyond v0.8.0
- Exposing `stacking.folds` in SQL — deferred

## Self-Check: PASSED

All 8 required files exist on disk. All 3 task commits verified in git history.

| Check | Result |
|-------|--------|
| `crates/anofox-fcst-core/src/forecast.rs` | FOUND |
| `crates/anofox-fcst-ffi/src/types.rs` | FOUND |
| `crates/anofox-fcst-ffi/src/lib.rs` | FOUND |
| `src/scalar_functions/ts_forecast_scalar.cpp` | FOUND |
| `src/table_functions/ts_forecast_native.cpp` | FOUND |
| `src/include/anofox_fcst_ffi.h` | FOUND |
| `examples/forecasting/autoensemble.sql` | FOUND |
| `.planning/phases/04-autoensemble-surface-combination-methods/04-01-SUMMARY.md` | FOUND |
| commit 15cf981 (Task 1) | FOUND |
| commit 6bf9213 (Task 2) | FOUND |
| commit 6b7d31a (Task 3) | FOUND |
