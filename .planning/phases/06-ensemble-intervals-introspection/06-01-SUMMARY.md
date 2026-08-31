---
phase: 06-ensemble-intervals-introspection
plan: "01"
subsystem: forecasting
tags:
  - ensemble
  - introspection
  - insp-01
  - rust-ffi
  - scalar-function
  - sql-surface

dependency_graph:
  requires:
    - phase: 05-explicit-member-ensemble
      provides: "build_forecaster + Ensemble crate API + ScalarFunction dispatch pattern"
    - phase: 04-autoensemble-surface-combination-methods
      provides: "parse_combination_method + AutoEnsemble path + CombinationMethod re-export"
  provides:
    - "inspect_explicit_ensemble: builds Ensemble, fits, reads .weights() for all 6 methods"
    - "inspect_auto_ensemble: builds AutoEnsemble, reads all_scores()[..model_count()]"
    - "EnsembleInspectResult FFI struct + 2 exports + companion free fn"
    - "_ts_ensemble_inspect_native ScalarFunction (explicit-member weights)"
    - "_ts_auto_ensemble_inspect_native ScalarFunction (AutoEnsemble scores + optional weights)"
    - "ts_ensemble_inspect_by SQL macro (per-series GROUP BY, long-format member/weight rows)"
    - "ts_auto_ensemble_inspect_by SQL macro (+ rank CTE via ROW_NUMBER)"
    - "INSP-01 DoD tracer verified: Mean weights==1/k (sum 1), WeightedMSE weight NULL + score>0"
  affects:
    - "Phase 6 plan 02 (06-02): EPI-01 ensemble conformal intervals example + docs"

actuals:
  tokens: 18000
  tasks: 3
  commits: 3

tech-stack:
  added:
    - "Ensemble::weights() accessor (anofox-forecast 0.15.3) — post-fit per-member weights"
    - "AutoEnsemble::all_scores()[..model_count()] — selected members + MSE scores"
    - "EnsembleInspectResult #[repr(C)] — parallel arrays: member_names_buf, weights, scores"
  patterns:
    - "NULL weights pointer convention: signals absent weight column (AutoEnsemble non-Mean)"
    - "NULL scores pointer convention: signals absent score column (explicit-member)"
    - "combine_method='' → Mean (parse_combination_method maps empty to Mean, not WeightedMSE)"
    - "rank CTE: ROW_NUMBER() OVER (PARTITION BY group_col ORDER BY score) in macro body"

key-files:
  created:
    - "crates/anofox-fcst-core/src/forecast.rs (+inspect_explicit_ensemble, +inspect_auto_ensemble, +7 unit tests)"
    - "crates/anofox-fcst-ffi/src/lib.rs (+EnsembleInspectResult, +anofox_ts_ensemble_inspect, +anofox_ts_auto_ensemble_inspect, +anofox_free_ensemble_inspect_result)"
    - "src/include/ts_ensemble_inspect_native.hpp"
    - "src/table_functions/ts_ensemble_inspect_native.cpp"
    - "examples/forecasting/ensemble_inspect_tracer.sql"
  modified:
    - "crates/anofox-fcst-core/src/lib.rs (inspect_explicit_ensemble + inspect_auto_ensemble re-exports)"
    - "src/include/anofox_fcst_ffi.h (regenerated via make header — 4 new symbols)"
    - "CMakeLists.txt (ts_ensemble_inspect_native.cpp added to EXTENSION_SOURCES)"
    - "src/anofox_forecast_extension.cpp (#include + RegisterTsEnsembleInspectNativeFunction)"
    - "src/macros/ts_macros.cpp (ts_ensemble_inspect_by + ts_auto_ensemble_inspect_by macros)"

key-decisions:
  - "Two separate functions: ts_ensemble_inspect_by (explicit-member) + ts_auto_ensemble_inspect_by (AutoEnsemble) — inputs differ materially (members[] vs top_k), backend calls differ, output semantics differ"
  - "NULL weights pointer signals absent weight column to C++: insp_result.weights==null → Value(LogicalType::DOUBLE) NULL for all rows (AutoEnsemble non-Mean)"
  - "NULL scores pointer signals absent score column: explicit-member path returns scores=null (no per-member MSE)"
  - "rank CTE derived in macro body via ROW_NUMBER() OVER (PARTITION BY group_col ORDER BY score) — no FFI change"
  - "combination_method='' maps to Mean (not WeightedMSE): parse_combination_method maps empty string to Mean; users must pass 'weighted_mse' explicitly for that combination method on AutoEnsemble"
  - "Both ScalarFunctions in ONE .cpp file: ts_ensemble_inspect_native.cpp registers both via RegisterTsEnsembleInspectNativeFunction — keeps change count bounded"

requirements-completed:
  - INSP-01

coverage:
  - id: D1
    description: "ts_ensemble_inspect_by(['AutoARIMA','AutoETS','Naive'], combination_method:='mean') returns weight==1/3 for all members, sum==1 per series"
    requirement: INSP-01
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_inspect_tracer.sql Section 1 — unequal_mean_weights=0, bad_weight_sum=0"
        status: pass
    human_judgment: false
  - id: D2
    description: "ts_auto_ensemble_inspect_by(combination_method:='weighted_mse') returns weight IS NULL for all rows, score>0, rank in (1,2,3)"
    requirement: INSP-01
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_inspect_tracer.sql Section 2 — non_null_weights=0, bad_scores=0, bad_ranks=0"
        status: pass
    human_judgment: false
  - id: D3
    description: "ts_auto_ensemble_inspect_by(combination_method:='mean') returns weight==1/k, score>0 for all rows"
    requirement: INSP-01
    verification:
      - kind: e2e
        ref: "examples/forecasting/ensemble_inspect_tracer.sql Bonus section — null_weights=0"
        status: pass
    human_judgment: false
  - id: D4
    description: "7 unit tests pass: Mean==1/k, WeightedMSE sum==1, <2 members error, unknown member error, AutoEnsemble Mean Some(1/k), WeightedMSE None, top_k bound"
    requirement: INSP-01
    verification:
      - kind: unit
        ref: "cargo test -p anofox-fcst-core inspect_ — 7 passed 0 failed"
        status: pass
    human_judgment: false
  - id: D5
    description: "CMakeLists.txt contains ts_ensemble_inspect_native.cpp in EXTENSION_SOURCES"
    requirement: INSP-01
    verification:
      - kind: integration
        ref: "grep -c ts_ensemble_inspect_native.cpp CMakeLists.txt == 1"
        status: pass
    human_judgment: false
  - id: D6
    description: "anofox_fcst_ffi.h contains all 4 INSP-01 symbols (EnsembleInspectResult, 2 exports, free fn)"
    requirement: INSP-01
    verification:
      - kind: integration
        ref: "grep -c symbols src/include/anofox_fcst_ffi.h == 16 (across all occurrences)"
        status: pass
    human_judgment: false

duration: "9m 32s"
completed: "2026-08-31"
status: complete
---

# Phase 06 Plan 01: Ensemble Member Introspection (INSP-01) Summary

**Two introspection Rust functions + EnsembleInspectResult FFI struct + 2 FFI exports + companion free fn + 2 C++ ScalarFunctions + CMakeLists entry + registration + 2 SQL macros, end-to-end verified: explicit Mean weights==1/k (sum 1), AutoEnsemble WeightedMSE weight IS NULL + score>0 + rank**

## Performance

- **Duration:** 9m 32s
- **Started:** 2026-08-31T12:41:03Z
- **Completed:** 2026-08-31T12:50:35Z
- **Tasks:** 3 / 3
- **Files modified:** 10 (5 created + 5 modified + 1 regenerated)

## Accomplishments

- `inspect_explicit_ensemble(&[Option<f64>], &[String], Option<&str>, usize) -> Result<Vec<(String, f64)>>`: validates member count >= 2, reuses `parse_combination_method` (Phase 4), maps names via `ModelType::from_str` through `build_forecaster`, builds `Ensemble::new(members).with_method(method)`, calls `.fit(&ts)`, reads `.weights()` AFTER fit (pre-fit weights are always uniform — RESEARCH Pitfall 2 avoided)
- `inspect_auto_ensemble(&[f64], usize, Option<&str>, usize) -> Result<Vec<(String, f64, Option<f64>)>>`: builds `AutoEnsemble::with_config(top_k, method, period)`, calls `.fit(&ts)`, reads `all_scores().iter().take(model_count())`. Weight is `Some(1/k)` for Mean, `None` for all other methods (crate 0.15.3 limitation documented)
- 7 unit tests all pass covering all behavior cases from the plan
- `EnsembleInspectResult #[repr(C)]` struct with null-delimited member names buffer + parallel weights/scores arrays; null pointer signals absent column
- `anofox_ts_ensemble_inspect` FFI: null-delimited buffer parse with defensive members_count assertion (T-06-01 mitigation); weights array allocated; scores=null; catch_unwind panic safety
- `anofox_ts_auto_ensemble_inspect` FFI: build_series → Vec<f64> conversion; null weights pointer for non-Mean (T-06-02 mitigation)
- `anofox_free_ensemble_inspect_result`: frees all three buffers and zeroes struct (T-06-05 mitigation)
- `make header` regenerated: 16 occurrences of 4 new symbols in `src/include/anofox_fcst_ffi.h`
- `_ts_ensemble_inspect_native` ScalarFunction: LIST(DOUBLE) values + LIST(VARCHAR) members → LIST(STRUCT(member_name VARCHAR, weight DOUBLE, score DOUBLE))
- `_ts_auto_ensemble_inspect_native` ScalarFunction: LIST(DOUBLE) values + INTEGER top_k → same return type; NULL weight pointer → NULL DOUBLE column
- CMakeLists.txt: `ts_ensemble_inspect_native.cpp` added to EXTENSION_SOURCES immediately after Phase 5 entry
- `ts_ensemble_inspect_by` macro: per-series GROUP BY + unnest, named defaults `combination_method := ''`, `seasonal_period := 0`; score column NULL for explicit-member
- `ts_auto_ensemble_inspect_by` macro: same shape + `rank` CTE via `ROW_NUMBER() OVER (PARTITION BY group_col ORDER BY score)` — no FFI change needed
- Extension builds clean (GCC 16 SFINAE warnings are pre-existing, unrelated to this phase)
- Tracer verified against built extension: all assertion counts == 0

## Task Commits

1. **Task 1: Rust core — inspect_explicit_ensemble + inspect_auto_ensemble** — `9ab9439` (feat)
2. **Task 2: FFI EnsembleInspectResult + 2 exports + free fn + make header** — `01fc01d` (feat)
3. **Task 3: C++ ScalarFunctions + CMakeLists + registration + macros + tracer** — `d9ca16f` (feat)

## Files Created/Modified

- `crates/anofox-fcst-core/src/forecast.rs` — +274 lines: `inspect_explicit_ensemble` + `inspect_auto_ensemble` + 7 unit tests
- `crates/anofox-fcst-core/src/lib.rs` — added both new fns to pub re-exports
- `crates/anofox-fcst-ffi/src/lib.rs` — +384 lines: `EnsembleInspectResult` + 2 exports + free fn
- `src/include/anofox_fcst_ffi.h` — regenerated via `make header` (4 new symbols, 16 occurrences)
- `src/include/ts_ensemble_inspect_native.hpp` — new: declares `RegisterTsEnsembleInspectNativeFunction`
- `src/table_functions/ts_ensemble_inspect_native.cpp` — new: two ScalarFunctions (~330 lines)
- `CMakeLists.txt` — ts_ensemble_inspect_native.cpp added to EXTENSION_SOURCES
- `src/anofox_forecast_extension.cpp` — #include + Register call
- `src/macros/ts_macros.cpp` — ts_ensemble_inspect_by + ts_auto_ensemble_inspect_by macros (+80 lines)
- `examples/forecasting/ensemble_inspect_tracer.sql` — new: DoD tracer

## Decisions Made

- **Two-function design:** `ts_ensemble_inspect_by` (explicit-member) + `ts_auto_ensemble_inspect_by` (AutoEnsemble) as separate functions — inputs differ materially (members[] vs top_k), backend calls differ (Ensemble vs AutoEnsemble), output semantics differ (weight without score vs score with optional weight)
- **NULL pointer convention:** `weights=null` in `EnsembleInspectResult` signals "no weight available" (AutoEnsemble non-Mean); C++ checks null and emits `Value(LogicalType::DOUBLE)` NULL. Same for `scores=null` on the explicit-member path.
- **rank CTE in macro:** `ROW_NUMBER() OVER (PARTITION BY group_col ORDER BY score)` added to `ts_auto_ensemble_inspect_by` macro body — adds useful rank column at zero FFI cost
- **Both ScalarFunctions in one .cpp:** `ts_ensemble_inspect_native.cpp` registers both via a single `RegisterTsEnsembleInspectNativeFunction(loader)` call, consistent with the plan and keeping file count bounded

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Behavior] combination_method='' maps to Mean (not WeightedMSE) for AutoEnsemble**
- **Found during:** Task 3 tracer run — `non_null_weights=6` instead of 0 when using `combination_method := ''`
- **Issue:** `parse_combination_method` maps empty string `""` to `CombinationMethod::Mean` (line 2535: `"" | "mean" => Ok(CombinationMethod::Mean)`). This is existing behavior — the crate's default for `AutoEnsembleConfig` is `WeightedMSE`, but the SQL API uses `parse_combination_method` which maps `""` to `Mean`. This is consistent with the explicit-member ensemble behavior.
- **Fix:** Updated tracer to use `combination_method := 'weighted_mse'` explicitly for the non-Mean assertion. Updated tracer comment to document this. The macro behavior ('' → Mean) is correct and consistent; the DoD SQL assertion just needed to use the explicit method string.
- **Files modified:** `examples/forecasting/ensemble_inspect_tracer.sql`
- **Committed in:** d9ca16f
- **Impact:** The `ts_auto_ensemble_inspect_by` macro works correctly for both Mean (weight=1/k) and explicit WeightedMSE (weight=NULL). The default behavior (empty string) is Mean, which is also consistent with `ts_forecast_ensemble_by` and `ts_forecast_by` defaults.

## Known Stubs

None. The INSP-01 surface is fully wired end-to-end and tracer-verified. The following are deferred per plan design (not stubs):
- Per-step HorizonAdaptive weight matrix — `weights()` returns the average per-horizon weight (documented in function docstring)
- AutoEnsemble inner combination weights for WeightedMSE/InverseAIC/Stacking/HorizonAdaptive — crate 0.15.3 upstream limitation (documented in function docstring and macro description)

## Threat Surface Scan

No new threat surface beyond what is documented in the plan's `<threat_model>`:
- T-06-01 (members_buf over-read): MITIGATED — `members_buf_len` bounds the slice in FFI; no NUL-scan-for-length; `members_count` assertion catches marshalling bugs
- T-06-02 (EnsembleInspectResult buffer read-back): MITIGATED — `count` field bounds all reads; null pointer checks before array access
- T-06-03 (Rust panic crossing FFI): MITIGATED — `catch_unwind(AssertUnwindSafe(...))` wraps both FFI functions
- T-06-04 (member name injection): ACCEPTED — closed-36-name ModelType whitelist via `from_str`
- T-06-05 (memory leak): MITIGATED — `anofox_free_ensemble_inspect_result` frees all three buffers; C++ calls it after every successful unpack

## Self-Check: PASSED

All 10 files created/modified verified:
- `inspect_explicit_ensemble` and `inspect_auto_ensemble` in forecast.rs: present
- `inspect_explicit_ensemble` and `inspect_auto_ensemble` in lib.rs re-exports: present
- `EnsembleInspectResult` in FFI lib.rs: present
- All 4 symbols in anofox_fcst_ffi.h: 16 matches confirmed
- `ts_ensemble_inspect_native.hpp` in src/include/: present
- `ts_ensemble_inspect_native.cpp` in src/table_functions/: present
- `ts_ensemble_inspect_native.cpp` in CMakeLists.txt: grep -c == 1
- `RegisterTsEnsembleInspectNativeFunction` in extension.cpp: grep -c == 1
- `ts_ensemble_inspect_by` and `ts_auto_ensemble_inspect_by` in macros: grep -c == 10
- `ensemble_inspect_tracer.sql` in examples/forecasting/: present

All 3 task commits verified in git log: 9ab9439, 01fc01d, d9ca16f
