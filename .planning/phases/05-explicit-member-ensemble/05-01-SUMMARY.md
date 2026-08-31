---
phase: 05-explicit-member-ensemble
plan: "01"
subsystem: forecasting
tags:
  - ensemble
  - explicit-members
  - build_forecaster
  - rust-ffi
  - scalar-function
  - sql-surface

dependency_graph:
  requires:
    - phase: 04-autoensemble-surface-combination-methods
      provides: "parse_combination_method + Ensemble crate API + extract_forecast pattern"
  provides:
    - "build_forecaster: compiler-exhaustive 36-variant ModelType → BoxedForecaster factory"
    - "forecast_explicit_ensemble: per-series explicit-member ensemble with Mean/all-6-methods"
    - "anofox_ts_forecast_ensemble FFI export (null-delimited member buffer, explicit members_buf_len)"
    - "_ts_forecast_ensemble_native ScalarFunction (LIST(VARCHAR) members, unnest shape)"
    - "ts_forecast_ensemble_by SQL macro (per-series GROUP BY, combination_method, seasonal_period)"
    - "ENS-02 tracer verified: mismatch_count=0, NULL intervals, model_name='Ensemble'"
  affects:
    - "Phase 5 plan 02 (05-02): full 26-member allowlist smoke tests, all 6 methods, error tests, docs"
    - "Phase 6 (EPI-01): ensemble prediction intervals (yhat_lower/yhat_upper currently NULL)"
    - "Phase 6 (INSP-01): member/weight introspection (Ensemble::weights() reachable)"

actuals:
  tokens: 13500
  tasks: 3
  commits: 3

tech-stack:
  added:
    - "Ensemble::new(Vec<Box<dyn Forecaster>>) crate API (anofox-forecast 0.15.3)"
    - "BoxedForecaster type alias from anofox_forecast::models"
    - "build_forecaster factory: 26 supported ModelType variants boxed, 10 blocked with InvalidParameter"
  patterns:
    - "Null-delimited VARCHAR[] FFI marshalling: members_buf + explicit members_buf_len (T-05-01 mitigation)"
    - "_ts_forecast_scalar ScalarFunction precedent for per-series _by macros (critical dispatch note)"
    - "Inline date helpers (ExtractDateMicros, MicrosToDateValue, ComputeForecastDate) in .cpp for portability"
    - "forecast_explicit_ensemble: filter Option<f64> nulls inline, build Ensemble, extract_forecast"

key-files:
  created:
    - "crates/anofox-fcst-core/src/forecast.rs (build_forecaster + forecast_explicit_ensemble sections)"
    - "crates/anofox-fcst-ffi/src/lib.rs (anofox_ts_forecast_ensemble section)"
    - "src/include/ts_forecast_ensemble_native.hpp"
    - "src/table_functions/ts_forecast_ensemble_native.cpp"
    - "examples/forecasting/ensemble_explicit_tracer.sql"
  modified:
    - "crates/anofox-fcst-core/src/lib.rs (forecast_explicit_ensemble pub export)"
    - "src/include/anofox_fcst_ffi.h (regenerated via make header)"
    - "CMakeLists.txt (ts_forecast_ensemble_native.cpp added to EXTENSION_SOURCES)"
    - "src/anofox_forecast_extension.cpp (#include + Register* call)"
    - "src/macros/ts_macros.cpp (ts_forecast_ensemble_by macro added)"

key-decisions:
  - "ScalarFunction dispatch (not TableFunction): _ts_forecast_ensemble_native registered via ScalarFunction per _ts_forecast_scalar precedent; the ts_forecast_ensemble_by macro uses unnest(..., recursive:=true) under GROUP BY, matching the proven per-series shape"
  - "Header location: src/include/ts_forecast_ensemble_native.hpp (not src/table_functions/) following actual project pattern for Phase 2/3 table function headers"
  - "Inline date helpers: ExtractDateMicros / MicrosToDateValue / ComputeForecastDate inlined as lambdas in the .cpp rather than sharing across TU (avoids cross-TU static symbol issues)"
  - "Ensemble implements Forecaster: confirmed at anofox-forecast-0.15.3/src/models/ensemble/model.rs:575; extract_forecast(&ens, h, 'Ensemble') works directly"
  - "SESOptimized constructor: SimpleExponentialSmoothing::auto() (not .optimized()) — confirmed from forecast_ses_optimized inline fn"
  - "DynamicOptimizedTheta: DynamicTheta::optimized() / DynamicTheta::seasonal_optimized(p) — confirmed from forecast_dynamic_optimized_theta inline fn"
  - "SeasonalESOptimized: SeasonalESModel::optimized(p) (static method) — confirmed from forecast_seasonal_es_optimized"
  - "ETS in build_forecaster: ETSModel::new(ETSSpec::aaa(), p) for seasonal, ETSModel::default() otherwise"
  - "forecast_explicit_ensemble takes &[Option<f64>] to match the existing forecast() FFI boundary; nulls filtered inline"

patterns-established:
  - "build_forecaster pattern: exhaustive ModelType match returning BoxedForecaster; blocked variants return InvalidParameter naming the member + suggesting alternative; safe foundation for Phase 05-02 expansion"
  - "Null-delimited member buffer: C++ builds members_buf with '\0' separators, passes .data()/.size() to Rust; Rust splits on b==0 with !is_empty() filter; defensive members_count assertion catches marshalling bugs"

requirements-completed:
  - ENS-02

coverage:
  - id: D1
    description: "ts_forecast_ensemble_by(['AutoARIMA','AutoETS','Naive'], combination_method:='mean') returns blended per-series forecast (5 rows) with model_name='Ensemble'"
    requirement: ENS-02
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_explicit_tracer.sql — Section 1 ensemble result (5 forecast rows, model_name=Ensemble)"
        status: pass
    human_judgment: false
  - id: D2
    description: "Mean combination_method cross-check: ensemble yhat == arithmetic mean of AutoARIMA+AutoETS+Naive independent forecasts within 1e-6"
    requirement: ENS-02
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_explicit_tracer.sql — mismatch_count=0 (all 5 steps diff=0.0)"
        status: pass
    human_judgment: false
  - id: D3
    description: "yhat_lower and yhat_upper are NULL (point-only in Phase 5, EPI-01 deferred)"
    requirement: ENS-02
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_explicit_tracer.sql — non_null_intervals=0"
        status: pass
    human_judgment: false
  - id: D4
    description: "build_forecaster is compiler-exhaustive over all 36 ModelType variants; 10 blocked variants return InvalidParameter"
    requirement: ENS-02
    verification:
      - kind: unit
        ref: "cargo build -p anofox-fcst-core — exhaustive match enforced by Rust compiler (no wildcard arm)"
        status: pass
    human_judgment: false
  - id: D5
    description: "anofox_ts_forecast_ensemble appears in regenerated C header src/include/anofox_fcst_ffi.h with members_buf_len parameter"
    requirement: ENS-02
    verification:
      - kind: integration
        ref: "make header — grep anofox_ts_forecast_ensemble returns 1; grep members_buf_len returns 2"
        status: pass
    human_judgment: false
  - id: D6
    description: "CMakeLists.txt contains ts_forecast_ensemble_native.cpp in EXTENSION_SOURCES"
    requirement: ENS-02
    verification:
      - kind: integration
        ref: "grep -c ts_forecast_ensemble_native.cpp CMakeLists.txt == 1"
        status: pass
    human_judgment: false

duration: "13m 31s"
completed: "2026-08-31"
status: complete
---

# Phase 05 Plan 01: Explicit-Member Ensemble Tracer Slice Summary

**Rust `build_forecaster` factory (exhaustive 36-variant match) + `forecast_explicit_ensemble` + `anofox_ts_forecast_ensemble` FFI (null-delimited member buffer) + `_ts_forecast_ensemble_native` ScalarFunction + `ts_forecast_ensemble_by` macro, end-to-end verified: Mean cross-check mismatch_count=0 on ['AutoARIMA','AutoETS','Naive'] with NULL intervals**

## Performance

- **Duration:** 13 min 31s
- **Started:** 2026-08-31T06:34:48Z
- **Completed:** 2026-08-31T06:48:19Z
- **Tasks:** 3 / 3
- **Files modified:** 10 (9 modified + 1 regenerated)

## Accomplishments

- `build_forecaster(ModelType, Option<usize>) -> Result<BoxedForecaster>` — compiler-exhaustive 36-variant match; 26 models boxed correctly using exact constructors confirmed from inline `forecast_*` sub-functions; 10 blocked models (GARCH, Laplace, ARIMA, MFLES, AutoMFLES, MSTL, AutoMSTL, TBATS, AutoTBATS, AutoEnsemble) return `InvalidParameter` naming the member
- `forecast_explicit_ensemble(&[Option<f64>], horizon, &[String], Option<&str>, usize) -> Result<ForecastOutput>` — validates member count >= 2, reuses `parse_combination_method` (Phase 4), maps names via `ModelType::from_str`, calls `Ensemble::new(members).with_method(method)`, returns model_name="Ensemble"
- `anofox_ts_forecast_ensemble` FFI export — null-delimited concatenated member buffer + explicit `members_buf_len` (T-05-01 mitigation: no over-read); catch_unwind panic safety; NULL lower/upper bounds (EPI-01 deferred)
- `_ts_forecast_ensemble_native` ScalarFunction (follows `_ts_forecast_scalar` precedent as required by the `<critical_dispatch_note>`) — LIST(VARCHAR) members extracted per-row, null-delimited buffer built, `anofox_ts_forecast_ensemble` called, yhat_lower/yhat_upper emit NULL
- `ts_forecast_ensemble_by` macro — per-series GROUP BY + unnest shape, named defaults `combination_method := ''` and `seasonal_period := 0`
- Tracer verified against built extension: diff=0.0 on all 5 forecast steps (exact, not just < 1e-6 tolerance), NULL intervals confirmed, model_name='Ensemble'

## Task Commits

1. **Task 1: Rust core — build_forecaster + forecast_explicit_ensemble** — `b4c1f83` (feat)
2. **Task 2: FFI export + make header** — `3728282` (feat)
3. **Task 3: C++ ScalarFunction + CMakeLists + registration + macro + verified tracer** — `cf6e34b` (feat)

## Files Created/Modified

- `crates/anofox-fcst-core/src/forecast.rs` — +268 lines: `build_forecaster` + `forecast_explicit_ensemble`
- `crates/anofox-fcst-core/src/lib.rs` — added `forecast_explicit_ensemble` to pub re-exports
- `crates/anofox-fcst-ffi/src/lib.rs` — +145 lines: `anofox_ts_forecast_ensemble` FFI export
- `src/include/anofox_fcst_ffi.h` — regenerated via `make header` (anofox_ts_forecast_ensemble + members_buf_len)
- `src/include/ts_forecast_ensemble_native.hpp` — new: declares `RegisterTsForecastEnsembleNativeFunction`
- `src/table_functions/ts_forecast_ensemble_native.cpp` — new: ScalarFunction implementation (+459 lines)
- `CMakeLists.txt` — ts_forecast_ensemble_native.cpp added to EXTENSION_SOURCES
- `src/anofox_forecast_extension.cpp` — #include + Register* call
- `src/macros/ts_macros.cpp` — ts_forecast_ensemble_by macro added (+38 lines)
- `examples/forecasting/ensemble_explicit_tracer.sql` — new: DoD cross-check tracer

## Decisions Made

- **ScalarFunction, not TableFunction:** The `<critical_dispatch_note>` was heeded. Registered `_ts_forecast_ensemble_native` via `ScalarFunction` (per `_ts_forecast_scalar` precedent). The macro uses `unnest(..., recursive:=true)` under `GROUP BY`, identical to `ts_forecast_by`.
- **Header in src/include/:** Actual project pattern (Phase 2/3) puts headers for new table/scalar functions in `src/include/`, not `src/table_functions/`. Followed the actual pattern.
- **Inline date helpers:** `ExtractDateMicros`, `MicrosToDateValue`, `ComputeForecastDate` are static functions in `ts_forecast_scalar.cpp` — cannot be shared across TUs. Inlined as lambdas in the new .cpp.
- **forecast_explicit_ensemble takes &[Option<f64>]:** Matched the FFI boundary that builds `Vec<Option<f64>>` via `build_series`; nulls filtered inline before passing to `make_timeseries`.
- **`Ensemble` implements `Forecaster` confirmed:** Verified `anofox-forecast-0.15.3/src/models/ensemble/model.rs:575` — `impl Forecaster for Ensemble`. `extract_forecast(&ens, h, "Ensemble")` works directly.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Bug] SESOptimized constructor: `.auto()` not `.optimized()`**
- **Found during:** Task 1 (reading forecast_ses_optimized inline fn)
- **Issue:** RESEARCH skeleton suggested `SimpleExponentialSmoothing::new(0.3).optimized()`. Actual codebase uses `SimpleExponentialSmoothing::auto()` (no-arg static method).
- **Fix:** Used `SimpleExponentialSmoothing::auto()` in the `SESOptimized` arm of `build_forecaster`.
- **Files modified:** `crates/anofox-fcst-core/src/forecast.rs`
- **Committed in:** b4c1f83

**2. [Rule 1 - Bug] DynamicOptimizedTheta: `DynamicTheta::optimized()` / `::seasonal_optimized(p)` not `::new_dotm()`**
- **Found during:** Task 1 (reading forecast_dynamic_optimized_theta inline fn)
- **Issue:** RESEARCH skeleton suggested `DynamicTheta::new_dotm()`. Actual codebase uses `DynamicTheta::optimized()` (non-seasonal) and `DynamicTheta::seasonal_optimized(p)` (seasonal).
- **Fix:** Used the correct constructor pair in `build_forecaster`.
- **Files modified:** `crates/anofox-fcst-core/src/forecast.rs`
- **Committed in:** b4c1f83

**3. [Rule 1 - Bug] HoltWinters uses `.auto(p, SeasonalType::Additive)` not `.new(p)`**
- **Found during:** Task 1 (reading forecast_holt_winters_lib inline fn)
- **Issue:** RESEARCH skeleton suggested `HoltWintersModel::new(p)`. Actual codebase uses `HoltWintersModel::auto(p, SeasonalType::Additive)`.
- **Fix:** Used `HoltWintersModel::auto(p, SeasonalType::Additive)` in `build_forecaster`.
- **Files modified:** `crates/anofox-fcst-core/src/forecast.rs`
- **Committed in:** b4c1f83

**4. [Rule 1 - Bug] Header file goes in src/include/ not src/table_functions/**
- **Found during:** Task 3 (discovering actual project structure)
- **Issue:** Plan said to create `src/table_functions/ts_forecast_ensemble_native.hpp`. Actual project pattern (verified from Phase 2/3 .hpp files) puts headers in `src/include/`.
- **Fix:** Created `src/include/ts_forecast_ensemble_native.hpp`.
- **Files modified:** `src/include/ts_forecast_ensemble_native.hpp` (new location)
- **Committed in:** cf6e34b

**5. [Rule 3 - Blocking] Date helpers are static (non-shared) — inlined as lambdas**
- **Found during:** Task 3 (C++ build attempt with `ExtractDateMicros` reference)
- **Issue:** `ExtractDateMicros`, `MicrosToDateValue`, `ComputeForecastDate` are `static` functions inside `ts_forecast_scalar.cpp` and not accessible from a different TU.
- **Fix:** Inlined equivalent implementations as lambdas inside `TsForecastEnsembleNativeExecute`. The `ComputeForecastDate` lambda captures `freq_parsed`, `freq_seconds`, `freq_is_raw`, and `bind_data.date_col_type` from the enclosing scope.
- **Files modified:** `src/table_functions/ts_forecast_ensemble_native.cpp`
- **Committed in:** cf6e34b

---

**Total deviations:** 5 auto-fixed (3 constructor mismatches from RESEARCH skeleton — confirmed by reading inline fns as required, 1 project structure, 1 blocking C++ TU isolation issue)
**Impact on plan:** All auto-fixes were necessary for correctness and compilation. No scope creep. The RESEARCH.md explicitly flagged constructors A2-A4 as "verify during execution" — all were verified and corrected.

## Known Stubs

None. The tracer is fully wired end-to-end. Plan 05-02 will expand to the full 26-member allowlist smoke tests, all six combination method verifications, blocked-member error tests, and docs — those are explicitly deferred per plan scope.

The following are deferred per plan design (not stubs):
- Prediction intervals (`yhat_lower`/`yhat_upper`) — Phase 6, EPI-01
- Member/weight introspection (`Ensemble::weights()`) — Phase 6, INSP-01
- All 6 combination methods smoke test — Plan 05-02
- Blocked-member error tests — Plan 05-02
- Documentation — Plan 05-02

## Threat Surface Scan

No new threat surface beyond what is documented in the plan's `<threat_model>`:
- T-05-01 (members_buf over-read): MITIGATED — `members_buf_len` bounds the slice; no NUL-scan-for-length
- T-05-02 (< 2 members): MITIGATED — enforced in both C++ Bind (InvalidInputException) and Rust core (InvalidParameter)
- T-05-03 (panic across FFI): MITIGATED — `catch_unwind(AssertUnwindSafe(...))` wraps the entire Rust call
- T-05-04 (member name injection): ACCEPTED — closed-36-name ModelType whitelist via `from_str`; no dynamic dispatch on raw string

## Issues Encountered

None beyond the auto-fixed deviations documented above.

## Next Phase Readiness

- Phase 05-02 can proceed immediately: `build_forecaster` covers all 26 supported members at the compiler level; only runtime smoke tests for the non-tracer members are needed
- Phase 6 (EPI-01) can access `Ensemble::predict_with_intervals()` when ready — the current path uses `extract_forecast` (point-only); switching to `predict_with_intervals` is a localized change in `forecast_explicit_ensemble`
- The `ts_forecast_ensemble_by` macro is production-ready for the Mean combination method; all six methods route through the same `parse_combination_method` path

## Self-Check: PASSED

All 7 expected files present on disk. All 3 task commits verified in git log.
Content spot-checks: `forecast_explicit_ensemble` in lib.rs re-exports (1), `anofox_ts_forecast_ensemble` in header (1), `ts_forecast_ensemble_native.cpp` in CMakeLists (1), `ts_forecast_ensemble_by` in macros (3).
