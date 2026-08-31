---
phase: 05-explicit-member-ensemble
verified: 2026-08-31T12:10:00Z
status: passed
score: 8/8 must-haves verified
behavior_unverified: 0
overrides_applied: 0
reverified_after: "code-review fix bbfce39 (WR-01 NULL interpolation, WR-02 <3 min-length guard, WR-03 SMA window, + unit tests). Orchestrator independently re-ran both Phase 5 examples + the Phase 4 autoensemble example against the rebuilt extension post-fix: Mean cross-check diff=0.0/mismatch_count=0, six methods ok=true, error paths raise clear naming errors, no AutoEnsemble regression; cargo test 233 passed. Refreshed to post-date the fix."
---

# Phase 5: Explicit-Member Ensemble Verification Report

**Phase Goal:** SQL users name an explicit list of member models + a combination method via `ts_forecast_ensemble_by(...)`; the extension fits each named member per series and combines them, reusing Phase 4's combination plumbing.
**Requirement:** ENS-02
**Verified:** 2026-08-31T10:15:00Z
**Status:** PASSED
**Re-verification:** No — initial verification

---

## Goal Achievement

### Observable Truths

| # | Truth | Status | Evidence |
|---|-------|--------|----------|
| 1 | `ts_forecast_ensemble_by('table', group, ds, y, ['A','B','C'], h, freq, ...)` returns one blended forecast row-set per series; members as VARCHAR[]; model_name='Ensemble' | VERIFIED | Extension run: 5 rows returned, all with `model_name='Ensemble'`, correct group, ds, yhat columns. Independently executed against built extension. |
| 2 | For `combination_method := 'mean'`, ensemble == arithmetic mean of each named member's independent `ts_forecast_by` forecast within 1e-6 | VERIFIED | Tracer (AutoARIMA+AutoETS+Naive): mismatch_count=0, diff=0.0 exact on all 5 steps. Canonical DoD (AutoARIMA+AutoETS+Theta): mismatch_count=0, diff=0.0 exact on all 5 steps. Both cross-checks independently executed. |
| 3 | All six combination methods (mean, median, weighted_mse, inverse_aic, stacking, horizon_adaptive) produce finite non-NULL yhat | VERIFIED | Section 2 smoke test: 30 rows (6 methods × 5 steps), all `ok=true`; zero NULL/non-finite assertion returns 0 rows. |
| 4 | yhat_lower and yhat_upper are NULL (point forecasts only; intervals deferred to Phase 6 EPI-01) | VERIFIED | Tracer: non_null_intervals=0. Full example Section 1 non_null_intervals=0, Section 2 non_null_intervals=0. |
| 5 | Unknown member, <2 members, and blocked GARCH each raise a clear error naming the member | VERIFIED | (a) `'NotAModel'` → "Invalid parameter 'members' = 'NotAModel': unknown model name 'NotAModel'; use the same names as ts_forecast_by". (b) `['AutoARIMA']` → "ts_forecast_ensemble_by: at least 2 members are required. Got 1." (c) `'GARCH'` → "Invalid parameter 'members' = 'GARCH': GARCH is not supported as an ensemble member: Forecaster::predict() returns simulated innovations, not level forecasts; use AutoARIMA or AutoETS instead". All three independently executed. |
| 6 | Representative 26-member allowlist sample (Tier 1/2/3 including seasonal models) returns finite non-NULL yhat | VERIFIED | Section 3: SampleA [Naive,SES], SampleB [AutoARIMA,Theta,Holt], SampleC [CrostonClassic,ADIDA,IMAPA], SampleD [SeasonalNaive,HoltWinters] with seasonal_period=12 — all 4 groups report fails=0. |
| 7 | New C++ source is in CMakeLists EXTENSION_SOURCES, registered in extension.cpp, and wired end-to-end | VERIFIED | `grep -c 'ts_forecast_ensemble_native.cpp' CMakeLists.txt` = 1. `extension.cpp` line 7: `#include "ts_forecast_ensemble_native.hpp" // Phase 5: ENS-02`, line 180: `RegisterTsForecastEnsembleNativeFunction(loader); // Phase 5: ENS-02`. Macro body calls `_ts_forecast_ensemble_native` via `unnest(... GROUP BY group_col)`. |
| 8 | AutoEnsemble surface not regressed; `ts_forecast_by(...,'AutoEnsemble',...)` still works | VERIFIED | Spot-check: `ts_forecast_by('t', id, ds, y, 'AutoEnsemble', 3, '1d')` returns 3 rows with model_name='AutoEnsemble' and finite yhat. No regression. |

**Score:** 8/8 truths verified

---

### Required Artifacts

| Artifact | Expected | Status | Details |
|----------|----------|--------|---------|
| `crates/anofox-fcst-core/src/forecast.rs` | `fn build_forecaster(` and `fn forecast_explicit_ensemble(` | VERIFIED | Both present (grep count = 1 each); `forecast_explicit_ensemble` pub-re-exported from `lib.rs` |
| `crates/anofox-fcst-ffi/src/lib.rs` | `anofox_ts_forecast_ensemble` FFI export | VERIFIED | grep count = 1 (extern fn definition) |
| `src/include/anofox_fcst_ffi.h` | `anofox_ts_forecast_ensemble` + `members_buf_len` | VERIFIED | grep count = 1 (fn) and 2 (members_buf_len appears in signature + members_count line) |
| `src/include/ts_forecast_ensemble_native.hpp` | RegisterTsForecastEnsembleNativeFunction declaration | VERIFIED | File exists (363 bytes); declares `RegisterTsForecastEnsembleNativeFunction` |
| `src/table_functions/ts_forecast_ensemble_native.cpp` | ScalarFunction implementation | VERIFIED | File exists (20648 bytes, substantive); registered as ScalarFunction per `_ts_forecast_scalar` precedent |
| `CMakeLists.txt` | `ts_forecast_ensemble_native.cpp` in EXTENSION_SOURCES | VERIFIED | grep count = 1 |
| `src/macros/ts_macros.cpp` | `ts_forecast_ensemble_by` macro | VERIFIED | grep count = 3 (definition + body references); macro body wires to `_ts_forecast_ensemble_native` with correct GROUP BY + unnest shape |
| `src/anofox_forecast_extension.cpp` | `#include` + `Register*` call | VERIFIED | Line 7: include; line 180: RegisterTsForecastEnsembleNativeFunction |
| `examples/forecasting/ensemble_explicit_tracer.sql` | Tracer example runs clean | VERIFIED | Independently executed: mismatch_count=0, non_null_intervals=0, model_name='Ensemble' |
| `examples/forecasting/ensemble_explicit.sql` | Full DoD example (4 sections) | VERIFIED | Sections 1-3 independently executed and pass; Section 4 error paths independently verified |
| `docs/reference/models/ensemble/ensemble_explicit.md` | Reference doc with ts_forecast_ensemble_by | VERIFIED | File exists (14795 bytes); grep count = 18 occurrences of ts_forecast_ensemble_by |
| `docs/api/07-forecasting.md` | ts_forecast_ensemble_by entry | VERIFIED | grep count = 6 occurrences of ts_forecast_ensemble_by |

---

### Key Link Verification

| From | To | Via | Status | Details |
|------|----|----|--------|---------|
| `ts_forecast_ensemble_by` SQL macro | `_ts_forecast_ensemble_native` ScalarFunction | `unnest(... GROUP BY group_col)` in macro body | VERIFIED | Macro body directly calls `_ts_forecast_ensemble_native(LIST(date_col), LIST(target_col::DOUBLE), members, horizon, frequency, combination_method, seasonal_period)` |
| `_ts_forecast_ensemble_native` C++ | `anofox_ts_forecast_ensemble` FFI | null-delimited `members_buf` + explicit `members_buf_len` | VERIFIED | C++ builds null-delimited buffer; passes `.data()` and `.size()` to FFI; FFI bounds slice by `members_buf_len` |
| `anofox_ts_forecast_ensemble` FFI | `forecast_explicit_ensemble` Rust core | `anofox_fcst_core::forecast_explicit_ensemble(...)` | VERIFIED | FFI calls core function; `forecast_explicit_ensemble` pub-exported from `lib.rs` |
| `forecast_explicit_ensemble` | `build_forecaster` per member | `name.parse::<ModelType>()` then `build_forecaster(model_type, period)` | VERIFIED | Function body calls `build_forecaster` per member; `parse_combination_method` (Phase 4 fn) reused — grep confirms only 1 definition |
| `build_forecaster` | `Ensemble::new(members).with_method(method)` | compiler-exhaustive 36-variant match | VERIFIED | `grep -c '"Ensemble"'` shows model_name literal in `extract_forecast` call at forecast.rs line 2861 |
| member VARCHAR[] | null-delimited members_buf across FFI | C++ `members_buf += name; members_buf += '\0'` | VERIFIED | Wiring confirmed by successful runtime cross-check: ensemble == arithmetic mean of named members |

---

### Data-Flow Trace (Level 4)

| Artifact | Data Variable | Source | Produces Real Data | Status |
|----------|---------------|--------|--------------------|--------|
| `ts_forecast_ensemble_native.cpp` | `yhat` per member | Rust `forecast_explicit_ensemble` via `anofox_ts_forecast_ensemble` FFI | Yes — per-series forecast from each named model via `build_forecaster` + `Ensemble::new` | FLOWING |
| `ts_forecast_ensemble_native.cpp` | `yhat_lower`, `yhat_upper` | FFI returns null pointers (EPI-01 deferred) | NULL (documented design decision, not a stub) | FLOWING |

---

### Behavioral Spot-Checks

| Behavior | Command | Result | Status |
|----------|---------|--------|--------|
| Ensemble blended forecast with model_name='Ensemble' and NULL intervals | Ran `ensemble_explicit_tracer.sql` against built extension | 5 rows, all model_name='Ensemble', yhat_lower=NULL, yhat_upper=NULL, mismatch_count=0 | PASS |
| Canonical DoD cross-check: ['AutoARIMA','AutoETS','Theta'] Mean == arithmetic mean within 1e-6 | Ran Section 1 of `ensemble_explicit.sql` against built extension | mismatch_count=0, diff=0.0 exact on all 5 steps | PASS |
| All six combination methods produce finite non-NULL yhat | Ran Section 2 of `ensemble_explicit.sql` | 30 rows ok=true, 0 NULL/non-finite failures, 0 non-null intervals | PASS |
| Member-allowlist sample (Tier 1/2/3 including seasonal) | Ran Section 3 of `ensemble_explicit.sql` | SampleA/B/C/D: all fails=0 | PASS |
| Unknown member error path | `['AutoARIMA','NotAModel']` against built extension | "Invalid parameter 'members' = 'NotAModel': unknown model name 'NotAModel'" | PASS |
| <2 members error path | `['AutoARIMA']` against built extension | "ts_forecast_ensemble_by: at least 2 members are required. Got 1." | PASS |
| Blocked GARCH error path | `['AutoARIMA','GARCH']` against built extension | "Invalid parameter 'members' = 'GARCH': GARCH is not supported ... use AutoARIMA or AutoETS instead" | PASS |
| AutoEnsemble regression check | `ts_forecast_by('t', id, ds, y, 'AutoEnsemble', 3, '1d')` | 3 rows, model_name='AutoEnsemble', finite yhat | PASS |

---

### Requirements Coverage

| Requirement | Source Plan | Description | Status | Evidence |
|-------------|-------------|-------------|--------|----------|
| ENS-02 | 05-01, 05-02 | User can produce explicit-member ensemble forecast per series by naming member models and a combination method | SATISFIED | `ts_forecast_ensemble_by` wired end-to-end; DoD cross-check passes; all 6 methods smoke-tested; 3 error paths verified; docs present with verified snippets |

REQUIREMENTS.md traceability row for ENS-02 shows "Phase 5 | Complete" — verified as accurate.

---

### Anti-Patterns Found

No debt markers (TBD, FIXME, XXX) in any phase-modified file. No TODO/HACK/PLACEHOLDER in source files. No stubs: all yhat values flow from real Rust forecaster calls through the FFI boundary; NULL intervals are a documented design decision (EPI-01 deferral), not a stub.

| File | Line | Pattern | Severity | Impact |
|------|------|---------|----------|--------|
| — | — | None found | — | — |

---

### Human Verification Required

None. All must-haves are verified by running the extension against the built binary and observing output. No visual, real-time, or external-service behavior required.

---

## Gaps Summary

No gaps. All 8 observable truths verified. Phase goal achieved.

- ENS-02 functional goal: COMPLETE. `ts_forecast_ensemble_by` accepts a VARCHAR[] member list and a combination_method, fits each named member per series, combines via the specified method (reusing Phase 4's `parse_combination_method`), and returns one blended forecast row-set per series with model_name='Ensemble'.
- DoD cross-check: independently re-executed and confirmed — both tracer (AutoARIMA+AutoETS+Naive) and canonical (AutoARIMA+AutoETS+Theta) Mean cross-checks return mismatch_count=0 with diff=0.0 exact.
- Six combination methods: independently verified against the built extension — all 30 (6×5) rows return finite non-NULL yhat with NULL intervals.
- Error paths: all three independently executed — unknown member, <2 members, and blocked GARCH each raise a clear, member-naming error.
- NULL intervals: confirmed in tracer and full example — yhat_lower=NULL, yhat_upper=NULL (EPI-01 deferred to Phase 6 as designed).
- Wiring: CMakeLists entry, extension.cpp include + Register* call, macro body, FFI boundary, and Rust core all confirmed substantive and connected.
- Documentation: reference doc and API entry exist with verified snippets (PR #230 rule).
- No regression: AutoEnsemble surface (Phase 4) confirmed still working.

---

_Verified: 2026-08-31T10:15:00Z_
_Verifier: Claude (gsd-verifier)_
